#pragma once

#include <QObject>
#include <QProcess>

class StreamingPreferences;

class SshHostLauncher : public QObject
{
    Q_OBJECT

public:
    ~SshHostLauncher() override;

    // Creates the singleton if needed. Must be called on the main thread.
    static SshHostLauncher* get();

    // Returns the singleton if it exists, without creating it.
    // Safe to call from any thread.
    static SshHostLauncher* getIfExists();

    // Launches the configured command on the host over SSH. Returns false
    // if the feature is disabled, unconfigured, or unsupported here.
    Q_INVOKABLE bool startHost();

    // Called once the host has come online, so a later intentional
    // termination isn't reported as a launch failure.
    Q_INVOKABLE void notifyHostOnline();

public slots:
    // Terminates the SSH process, which delivers SIGHUP to the remote
    // command. No-op if we didn't start one.
    void stopHost();

signals:
    void hostLaunchFailed(QString error);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();

private:
    explicit SshHostLauncher(StreamingPreferences* prefs);

    QString writeAskpassHelper();

    StreamingPreferences* m_Prefs;
    QProcess* m_Process;
    bool m_Launching;
    QString m_LastStderrText;

    static SshHostLauncher* s_Instance;
};
