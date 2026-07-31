#include "sshhostlauncher.h"

#include "path.h"
#include "settings/streamingpreferences.h"

#include <QFile>
#include <QProcessEnvironment>

#include <QtDebug>

#define ASKPASS_HELPER_NAME "moonlight-askpass.sh"

SshHostLauncher* SshHostLauncher::s_Instance = nullptr;

SshHostLauncher::SshHostLauncher(StreamingPreferences* prefs)
    : m_Prefs(prefs),
      m_Process(nullptr),
      m_Launching(false)
{
}

SshHostLauncher::~SshHostLauncher()
{
    // ~QProcess kills the SSH session if it's still running
    s_Instance = nullptr;
}

SshHostLauncher* SshHostLauncher::get()
{
    if (s_Instance == nullptr) {
        s_Instance = new SshHostLauncher(StreamingPreferences::get());
    }
    return s_Instance;
}

SshHostLauncher* SshHostLauncher::getIfExists()
{
    return s_Instance;
}

bool SshHostLauncher::startHost()
{
#ifdef Q_OS_UNIX
    if (!m_Prefs->sshAutoStart || m_Prefs->sshHost.isEmpty() ||
            m_Prefs->sshUsername.isEmpty() || m_Prefs->sshPassword.isEmpty()) {
        return false;
    }

    // If we already have an SSH session running, there's nothing to do
    if (m_Process != nullptr) {
        return true;
    }

    QString command = m_Prefs->sshCommand.isEmpty() ?
                QStringLiteral("sunshine") : m_Prefs->sshCommand;
    QString destination = m_Prefs->sshUsername + "@" + m_Prefs->sshHost;

    // The password only ever travels via the environment, never on a
    // command line or inside the askpass script itself.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("MOONLIGHT_SSH_PASSWORD", m_Prefs->sshPassword);
    env.insert("SSH_ASKPASS", writeAskpassHelper());
    env.insert("SSH_ASKPASS_REQUIRE", "force");

    m_Process = new QProcess(this);
    m_Process->setProcessEnvironment(env);
    connect(m_Process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SshHostLauncher::onProcessFinished);
    connect(m_Process, &QProcess::errorOccurred,
            this, &SshHostLauncher::onProcessErrorOccurred);
    connect(m_Process, &QProcess::readyReadStandardOutput,
            this, &SshHostLauncher::onReadyReadStandardOutput);
    connect(m_Process, &QProcess::readyReadStandardError,
            this, &SshHostLauncher::onReadyReadStandardError);

    m_Launching = true;
    m_LastStderrText.clear();

    qInfo() << "Starting host software over SSH:" << destination << command;

    // -tt forces a remote pty allocation, so terminating our ssh process
    // hangs up the session and delivers SIGHUP to the remote command.
    m_Process->start("ssh",
                     {"-tt",
                      "-o", "StrictHostKeyChecking=accept-new",
                      "-o", "ConnectTimeout=10",
                      "-o", "NumberOfPasswordPrompts=1",
                      destination,
                      command});
    return true;
#else
    qWarning() << "SSH host startup is not supported on this platform";
    return false;
#endif
}

void SshHostLauncher::notifyHostOnline()
{
    m_Launching = false;
}

void SshHostLauncher::stopHost()
{
    if (m_Process == nullptr) {
        return;
    }

    QProcess* process = m_Process;
    m_Process = nullptr;
    m_Launching = false;

    // This is an intentional termination, so detach the failure handlers
    process->disconnect(this);

    qInfo() << "Stopping SSH-launched host software";

    process->terminate();
    if (!process->waitForFinished(5000)) {
        process->kill();
        process->waitForFinished(1000);
    }
    delete process;
}

QString SshHostLauncher::writeAskpassHelper()
{
    Path::writeCacheFile(ASKPASS_HELPER_NAME,
                         "#!/bin/sh\nprintf '%s\\n' \"$MOONLIGHT_SSH_PASSWORD\"\n");

    QString helperPath = Path::getCacheFileInfo(ASKPASS_HELPER_NAME).absoluteFilePath();
    QFile::setPermissions(helperPath,
                          QFileDevice::ReadOwner |
                          QFileDevice::WriteOwner |
                          QFileDevice::ExeOwner);
    return helperPath;
}

void SshHostLauncher::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    bool launching = m_Launching;

    qInfo() << "SSH process exited with code" << exitCode;

    m_Process->deleteLater();
    m_Process = nullptr;
    m_Launching = false;

    // Only report a failure if the process died before the host came online
    if (launching) {
        if (exitStatus == QProcess::CrashExit) {
            emit hostLaunchFailed(tr("The SSH process was terminated unexpectedly."));
        }
        else if (!m_LastStderrText.isEmpty()) {
            emit hostLaunchFailed(m_LastStderrText);
        }
        else {
            emit hostLaunchFailed(tr("SSH exited with code %1.").arg(exitCode));
        }
    }
}

void SshHostLauncher::onProcessErrorOccurred(QProcess::ProcessError error)
{
    // Other errors are followed by finished() and handled there
    if (error == QProcess::FailedToStart) {
        qWarning() << "SSH process failed to start";

        m_Process->deleteLater();
        m_Process = nullptr;
        m_Launching = false;

        emit hostLaunchFailed(tr("The 'ssh' program could not be started. Check that OpenSSH is installed."));
    }
}

void SshHostLauncher::onReadyReadStandardOutput()
{
    if (m_Process == nullptr) {
        return;
    }

    QString text = QString::fromUtf8(m_Process->readAllStandardOutput()).trimmed();
    if (!text.isEmpty()) {
        qInfo() << "SSH:" << qPrintable(text);
    }
}

void SshHostLauncher::onReadyReadStandardError()
{
    if (m_Process == nullptr) {
        return;
    }

    QString text = QString::fromUtf8(m_Process->readAllStandardError()).trimmed();
    if (!text.isEmpty()) {
        qWarning() << "SSH:" << qPrintable(text);
        m_LastStderrText = text;
    }
}
