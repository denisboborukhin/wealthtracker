module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryFile>
#include <QTimer>

#include <functional>
#include <utility>

}

export module wt.zen_plugin_runner;

import wt.connectors;

export namespace wealthtracker::app {

class ZenPluginRunner final : public QObject {
  public:
    using Prompted = std::function<void(wealthtracker::connectors::ZenPluginPrompt)>;
    using Completed = std::function<void(wealthtracker::connectors::NormalizedBatch, QJsonObject)>;
    using Failed = std::function<void(QString)>;

    ZenPluginRunner(QString pluginId, Prompted prompted, Completed completed, Failed failed,
                    QObject* parent)
        : QObject(parent), plugin_id_(std::move(pluginId)), prompted_(std::move(prompted)),
          completed_(std::move(completed)), failed_(std::move(failed)),
          host_file_(QDir::tempPath() + QStringLiteral("/wealthtracker-zen-host-XXXXXX.mjs")) {
        host_file_.setAutoRemove(true);
        QFile resource(QStringLiteral(":/org/wealthtracker/runtime/scripts/zen_plugin_host.mjs"));
        QByteArray hostContents;
        if (resource.open(QIODevice::ReadOnly)) {
            hostContents = resource.readAll();
        }
        host_ready_ = !hostContents.isEmpty() && host_file_.open() &&
                      host_file_.write(hostContents) == hostContents.size() && host_file_.flush();
        hostContents.fill('\0');
        if (host_ready_) {
            host_ready_ =
                host_file_.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
        host_file_.close();

        process_.setProcessChannelMode(QProcess::SeparateChannels);
        process_.setProcessEnvironment(sanitizedEnvironment());
        connect(&process_, &QProcess::started, this, [this] { processStarted(); });
        connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
        connect(&process_, &QProcess::readyReadStandardError, this, [this] { drainErrorOutput(); });
        connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int, QProcess::ExitStatus) {
                    if (!terminal_) {
                        fail(QStringLiteral("Локальный процесс плагина завершился без результата"));
                    }
                });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            if (!terminal_) {
                fail(QStringLiteral("Не удалось запустить локальный процесс плагина"));
            }
        });
        timeout_.setSingleShot(true);
        timeout_.setInterval(10 * 60 * 1000);
        connect(&timeout_, &QTimer::timeout, this,
                [this] { fail(QStringLiteral("Плагин превысил лимит времени синхронизации")); });
    }

    ~ZenPluginRunner() override {
        eraseBuffers();
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(1'000);
        }
    }

    bool start(const QString& executable, const QByteArray& startMessage) {
        if (started_ || !host_ready_ || executable.isEmpty() || startMessage.isEmpty()) {
            return false;
        }
        started_ = true;
        pending_start_message_ = startMessage;
        process_.setProgram(executable);
        process_.setArguments({host_file_.fileName()});
        process_.start();
        return true;
    }

    bool answer(const QByteArray& message) {
        if (terminal_ || prompt_id_.isEmpty() || process_.state() != QProcess::Running ||
            message.isEmpty()) {
            return false;
        }
        if (process_.write(message) != message.size()) {
            fail(QStringLiteral("Не удалось передать ответ локальному плагину"));
            return false;
        }
        prompt_id_.clear();
        return true;
    }

    void cancel() {
        if (terminal_) {
            return;
        }
        terminal_ = true;
        timeout_.stop();
        eraseBuffers();
        process_.kill();
    }

  private:
    static constexpr qsizetype maximumBufferedOutputBytes = 8 * 1024 * 1024;
    static constexpr qsizetype maximumTotalOutputBytes = 32 * 1024 * 1024;

    static QProcessEnvironment sanitizedEnvironment() {
        const auto source = QProcessEnvironment::systemEnvironment();
        QProcessEnvironment result;
        for (const auto& key :
             {QStringLiteral("LANG"), QStringLiteral("LC_ALL"), QStringLiteral("LC_CTYPE"),
              QStringLiteral("TMPDIR"), QStringLiteral("TEMP"), QStringLiteral("TMP"),
              QStringLiteral("SystemRoot"), QStringLiteral("WINDIR")}) {
            if (source.contains(key)) {
                result.insert(key, source.value(key));
            }
        }
        return result;
    }

    void processStarted() {
        if (terminal_) {
            return;
        }
        if (process_.write(pending_start_message_) != pending_start_message_.size()) {
            fail(QStringLiteral("Не удалось запустить протокол локального плагина"));
            return;
        }
        pending_start_message_.fill('\0');
        pending_start_message_.clear();
        timeout_.start();
    }

    void readOutput() {
        auto chunk = process_.readAllStandardOutput();
        total_output_bytes_ += chunk.size();
        if (total_output_bytes_ > maximumTotalOutputBytes) {
            chunk.fill('\0');
            fail(QStringLiteral("Плагин превысил общий лимит вывода"));
            return;
        }
        output_ += chunk;
        chunk.fill('\0');
        if (output_.size() > maximumBufferedOutputBytes) {
            fail(QStringLiteral("Ответ плагина превысил безопасный лимит"));
            return;
        }
        qsizetype newline = -1;
        while (!terminal_ && (newline = output_.indexOf('\n')) >= 0) {
            auto line = output_.first(newline);
            output_.remove(0, newline + 1);
            auto event = wealthtracker::connectors::parseZenPluginEvent(line, plugin_id_);
            line.fill('\0');
            if (!event) {
                fail(event.error().message);
                return;
            }
            handleEvent(std::move(*event));
        }
    }

    void drainErrorOutput() {
        auto chunk = process_.readAllStandardError();
        total_error_bytes_ += chunk.size();
        chunk.fill('\0');
        if (total_error_bytes_ > maximumTotalOutputBytes) {
            fail(QStringLiteral("Плагин превысил лимит диагностического вывода"));
        }
    }

    void handleEvent(wealthtracker::connectors::ZenPluginEvent event) {
        using wealthtracker::connectors::ZenPluginEventKind;
        if (event.kind == ZenPluginEventKind::Prompt) {
            if (!prompt_id_.isEmpty()) {
                fail(QStringLiteral("Плагин запросил несколько кодов одновременно"));
                return;
            }
            prompt_id_ = event.prompt.requestId;
            prompted_(std::move(event.prompt));
            return;
        }
        terminal_ = true;
        timeout_.stop();
        process_.closeWriteChannel();
        eraseBuffers();
        if (event.kind == ZenPluginEventKind::Result) {
            completed_(std::move(event.batch), std::move(event.state));
        } else {
            failed_(event.message);
        }
    }

    void fail(const QString& message) {
        if (terminal_) {
            return;
        }
        terminal_ = true;
        timeout_.stop();
        eraseBuffers();
        process_.kill();
        failed_(message);
    }

    void eraseBuffers() {
        pending_start_message_.fill('\0');
        pending_start_message_.clear();
        output_.fill('\0');
        output_.clear();
    }

    QString plugin_id_;
    QString prompt_id_;
    Prompted prompted_;
    Completed completed_;
    Failed failed_;
    QProcess process_;
    QTimer timeout_;
    QTemporaryFile host_file_;
    QByteArray pending_start_message_;
    QByteArray output_;
    qsizetype total_output_bytes_ = 0;
    qsizetype total_error_bytes_ = 0;
    bool host_ready_ = false;
    bool started_ = false;
    bool terminal_ = false;
};

} // namespace wealthtracker::app
