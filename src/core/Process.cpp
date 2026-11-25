#include "core/Process.hpp"

#include "core/Strings.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace gpu_report {

namespace {

bool setNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void closeFd(int& fd)
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void appendLimited(std::string& target, const char* data, std::size_t size, std::size_t limit)
{
    if (target.size() >= limit) {
        return;
    }
    const std::size_t allowed = limit - target.size();
    target.append(data, size < allowed ? size : allowed);
}

} // namespace

std::string ProcessResult::firstErrorLine() const
{
    for (const std::string& line : splitLines(errorOutput)) {
        const std::string trimmed = trim(line);
        if (!trimmed.empty()) {
            return trimmed;
        }
    }
    for (const std::string& line : splitLines(output)) {
        const std::string trimmed = trim(line);
        if (!trimmed.empty()) {
            return trimmed;
        }
    }
    return {};
}

bool findExecutable(const std::string& name, std::string* path)
{
    if (name.empty()) {
        return false;
    }
    if (name.find('/') != std::string::npos) {
        if (access(name.c_str(), X_OK) == 0) {
            if (path != nullptr) {
                *path = name;
            }
            return true;
        }
        return false;
    }

    const char* pathEnvironment = std::getenv("PATH");
    const std::string searchPath = pathEnvironment != nullptr
        ? pathEnvironment
        : "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";

    for (const std::string& directory : split(searchPath, ':')) {
        if (directory.empty()) {
            continue;
        }
        const std::string candidate = directory + "/" + name;
        if (access(candidate.c_str(), X_OK) == 0) {
            if (path != nullptr) {
                *path = candidate;
            }
            return true;
        }
    }
    return false;
}

ProcessResult runProcess(const std::vector<std::string>& arguments,
                         int timeoutMs,
                         std::size_t maxOutputBytes)
{
    ProcessResult result;
    if (arguments.empty()) {
        return result;
    }

    int outputPipe[2] = {-1, -1};
    int errorPipe[2] = {-1, -1};
    if (pipe(outputPipe) != 0) {
        return result;
    }
    if (pipe(errorPipe) != 0) {
        ::close(outputPipe[0]);
        ::close(outputPipe[1]);
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        ::close(outputPipe[0]);
        ::close(outputPipe[1]);
        ::close(errorPipe[0]);
        ::close(errorPipe[1]);
        return result;
    }

    if (pid == 0) {
        // Put the child in its own process group so a timeout can terminate
        // the whole tree (some diagnostic tools spawn helpers).
        setpgid(0, 0);
        ::dup2(outputPipe[1], STDOUT_FILENO);
        ::dup2(errorPipe[1], STDERR_FILENO);
        ::close(outputPipe[0]);
        ::close(outputPipe[1]);
        ::close(errorPipe[0]);
        ::close(errorPipe[1]);

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    setpgid(pid, pid);

    ::close(outputPipe[1]);
    ::close(errorPipe[1]);

    int outputFd = outputPipe[0];
    int errorFd = errorPipe[0];
    setNonBlocking(outputFd);
    setNonBlocking(errorFd);

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 1);

    bool outputOpen = true;
    bool errorOpen = true;
    bool timedOut = false;
    char buffer[8192];

    while (outputOpen || errorOpen) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            timedOut = true;
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            break;
        }

        struct pollfd descriptors[2];
        descriptors[0].fd = outputOpen ? outputFd : -1;
        descriptors[0].events = POLLIN;
        descriptors[0].revents = 0;
        descriptors[1].fd = errorOpen ? errorFd : -1;
        descriptors[1].events = POLLIN;
        descriptors[1].revents = 0;

        const int pollResult = poll(descriptors, 2, 100);
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < 2; ++i) {
            const int fd = descriptors[i].fd;
            if (fd < 0 || descriptors[i].revents == 0) {
                continue;
            }
            if ((descriptors[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }

            while (true) {
                const ssize_t count = ::read(fd, buffer, sizeof(buffer));
                if (count > 0) {
                    if (i == 0) {
                        appendLimited(result.output, buffer, static_cast<std::size_t>(count),
                                      maxOutputBytes);
                    } else {
                        appendLimited(result.errorOutput, buffer, static_cast<std::size_t>(count),
                                      maxOutputBytes);
                    }
                    continue;
                }
                if (count == 0) {
                    if (i == 0) {
                        closeFd(outputFd);
                        outputOpen = false;
                    } else {
                        closeFd(errorFd);
                        errorOpen = false;
                    }
                    break;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                if (i == 0) {
                    closeFd(outputFd);
                    outputOpen = false;
                } else {
                    closeFd(errorFd);
                    errorOpen = false;
                }
                break;
            }
        }
    }

    closeFd(outputFd);
    closeFd(errorFd);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }

    result.started = true;
    result.timedOut = timedOut;
    if (timedOut) {
        result.exitCode = -1;
    } else if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
    }

    return result;
}

} // namespace gpu_report
