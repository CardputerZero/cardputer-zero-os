#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <pwd.h>
#include <security/pam_appl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kAuthFailed = 10;
constexpr int kSessionFailed = 20;
constexpr int kHelperFailed = 30;

bool shell_is_login_capable(const char *shell)
{
    if (shell == nullptr || shell[0] == '\0') {
        return true;
    }
    return std::strstr(shell, "nologin") == nullptr && std::strstr(shell, "false") == nullptr;
}

bool user_is_login_capable(const passwd *pw)
{
    if (pw == nullptr || pw->pw_name == nullptr || pw->pw_dir == nullptr) {
        return false;
    }
    if (pw->pw_uid < 1000 || pw->pw_uid >= 60000) {
        return false;
    }
    if (std::strncmp(pw->pw_dir, "/home/", 6) != 0) {
        return false;
    }
    return shell_is_login_capable(pw->pw_shell);
}

bool caller_is_allowed()
{
    if (geteuid() != 0) {
        return false;
    }
    if (getuid() == 0) {
        return true;
    }
    passwd *caller = getpwuid(getuid());
    return caller != nullptr && caller->pw_name != nullptr && std::strcmp(caller->pw_name, "_greetd") == 0;
}

bool read_request(std::string &username, std::string &password)
{
    std::string input;
    char buffer[256];
    for (;;) {
        ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            break;
        }
        input.append(buffer, buffer + count);
        if (input.size() > 4096) {
            return false;
        }
    }

    size_t first_newline = input.find('\n');
    if (first_newline == std::string::npos) {
        return false;
    }
    size_t second_newline = input.find('\n', first_newline + 1);
    if (second_newline == std::string::npos) {
        second_newline = input.size();
    }

    username = input.substr(0, first_newline);
    password = input.substr(first_newline + 1, second_newline - first_newline - 1);
    if (!username.empty() && username.back() == '\r') {
        username.pop_back();
    }
    if (!password.empty() && password.back() == '\r') {
        password.pop_back();
    }
    return !username.empty() && username.size() <= 64 && password.size() <= 256;
}

int pam_conv_callback(int num_msg, const pam_message **msg, pam_response **resp, void *appdata_ptr)
{
    if (num_msg <= 0 || msg == nullptr || resp == nullptr) {
        return PAM_CONV_ERR;
    }

    const auto *password = static_cast<const std::string *>(appdata_ptr);
    pam_response *responses = static_cast<pam_response *>(calloc(static_cast<size_t>(num_msg), sizeof(pam_response)));
    if (responses == nullptr) {
        return PAM_BUF_ERR;
    }

    for (int i = 0; i < num_msg; ++i) {
        if (msg[i] == nullptr) {
            free(responses);
            return PAM_CONV_ERR;
        }
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON:
                responses[i].resp = strdup(password != nullptr ? password->c_str() : "");
                if (responses[i].resp == nullptr) {
                    for (int j = 0; j < i; ++j) {
                        free(responses[j].resp);
                    }
                    free(responses);
                    return PAM_BUF_ERR;
                }
                break;
            case PAM_ERROR_MSG:
            case PAM_TEXT_INFO:
                break;
            default:
                for (int j = 0; j < i; ++j) {
                    free(responses[j].resp);
                }
                free(responses);
                return PAM_CONV_ERR;
        }
    }
    *resp = responses;
    return PAM_SUCCESS;
}

bool authenticate_user(const std::string &username, const std::string &password)
{
    pam_conv conv{pam_conv_callback, const_cast<std::string *>(&password)};
    pam_handle_t *pamh = nullptr;
    int rc = pam_start("cardputer-zero-login", username.c_str(), &conv, &pamh);
    if (rc != PAM_SUCCESS) {
        return false;
    }

    pam_set_item(pamh, PAM_TTY, "cardputer-zero-internal");
    pam_putenv(pamh, "XDG_SEAT=seat-cardputer-zero");
    pam_putenv(pamh, "XDG_SESSION_CLASS=user");
    pam_putenv(pamh, "XDG_SESSION_TYPE=wayland");

    rc = pam_authenticate(pamh, 0);
    if (rc == PAM_SUCCESS) {
        rc = pam_acct_mgmt(pamh, 0);
    }
    pam_end(pamh, rc);
    return rc == PAM_SUCCESS;
}

bool run_and_wait(std::vector<std::string> args)
{
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        clearenv();
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        execv(argv[0], argv.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool start_user_session(const passwd *pw)
{
    std::string timestamp = std::to_string(static_cast<unsigned long>(std::time(nullptr)));
    std::string unit = "cardputer-zero-session-" + std::to_string(static_cast<unsigned long>(pw->pw_uid)) + "-" + timestamp;

    return run_and_wait({
        "/usr/bin/systemd-run",
        "--unit=" + unit,
        "--description=Cardputer Zero internal user session",
        "--collect",
        "--property=User=" + std::string(pw->pw_name),
        "--property=PAMName=cardputer-zero-session",
        "--working-directory=" + std::string(pw->pw_dir),
        "--setenv=XDG_SEAT=seat-cardputer-zero",
        "--setenv=XDG_SESSION_CLASS=user",
        "--setenv=XDG_SESSION_TYPE=wayland",
        "--setenv=XDG_CURRENT_DESKTOP=CardputerZero",
        "--setenv=XDG_SESSION_DESKTOP=CardputerZero",
        "--setenv=DESKTOP_SESSION=cardputer-zero-labwc",
        "--setenv=CARDPUTER_ZERO_SESSION=1",
        "--setenv=WLR_BACKENDS=drm,libinput",
        "--setenv=WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal",
        "--setenv=WLR_RENDERER=pixman",
        "--setenv=XCURSOR_THEME=cardputer-zero-empty",
        "--setenv=XCURSOR_SIZE=1",
        "--setenv=XKB_DEFAULT_RULES=cardputerzero",
        "--setenv=XKB_DEFAULT_MODEL=pc105",
        "--setenv=XKB_DEFAULT_LAYOUT=cardputerzero",
        "--setenv=XKB_DEFAULT_VARIANT=",
        "--setenv=XKB_DEFAULT_OPTIONS=",
        "/bin/sh",
        "-c",
        "sleep 0.35; exec /usr/local/bin/cardputer-zero-session",
    });
}

void write_status(const char *status)
{
    std::cout << status << '\n';
}

} // namespace

int main()
{
    if (!caller_is_allowed()) {
        write_status("HELPER DENIED");
        return kHelperFailed;
    }

    if (setgid(0) != 0 || setuid(0) != 0) {
        write_status("HELPER DENIED");
        return kHelperFailed;
    }

    std::string username;
    std::string password;
    if (!read_request(username, password)) {
        write_status("HELPER DENIED");
        return kHelperFailed;
    }

    passwd *pw = getpwnam(username.c_str());
    if (!user_is_login_capable(pw)) {
        write_status("USER INVALID");
        return kAuthFailed;
    }

    if (!authenticate_user(username, password)) {
        write_status("AUTH FAILED");
        return kAuthFailed;
    }
    std::fill(password.begin(), password.end(), '\0');

    if (!start_user_session(pw)) {
        write_status("SESSION FAILED");
        return kSessionFailed;
    }

    write_status("OK");
    return 0;
}
