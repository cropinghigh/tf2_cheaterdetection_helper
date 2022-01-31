#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSettings>
#include <QThread>
#include <QStandardItemModel>
#include <QMap>
#include <vector>
#include <time.h>
#include <chrono>
#include <regex>
#include <algorithm>

#include "srcon.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT

#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class steamUser {
public:
    std::time_t timestamp;
    int visibility;
    bool configured;
    std::string name;
    std::string tfname;
    std::time_t created;
    std::string country;
    int playhours;
    int steamLevel;
    int vacs;
};

class UpdaterThreadWorker : public QThread {
    Q_OBJECT
public:
    UpdaterThreadWorker(QStandardItemModel* retTableElements);
    ~UpdaterThreadWorker();

    void run() override;
signals:
    void updateStatus(const QString& status, const QString& lastUpdate, const QString& connectedTo);
    void updateTable();

public slots:
    void on_rconpass_update(const QString& retRconPass);
    void on_apikey_update(const QString& retApiKey);
    void on_logfile_update(const QString& retLogfile);

private:
    steamUser getUserData(std::string steamid64, std::string name);
    bool processStatusOutput(const std::string status);
    void updateTime();

    QStandardItemModel* tableElements;
    QString rconpass;
    QString apiKey = "";
    QString logfile = "";
    QString lastConn = "None";
    std::streampos lastPosition = 0;
    std::ifstream logfile_str;
    std::string logfile_buffer;
    httplib::Client httpCli = httplib::Client("https://api.steampowered.com");
    QString lastUpdate;
    QMap<std::string, steamUser> steamUsersCache;
    std::mutex main_mtx;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_lineEdit_textChanged(const QString &arg1);
    void on_lineEdit_2_textChanged(const QString &arg1);

    void on_pushButton_clicked();

public slots:
    void on_status_update(const QString& status, const QString& lastUpdate, const QString& connectedTo);
    void on_table_update();

private:
    void updateTable();

    Ui::MainWindow *ui;
    QSettings settings;
    QString rconpass = "";
    QString apiKey = "";
    QString logfile = "";
    QStandardItemModel tableModel;

    UpdaterThreadWorker utw;
};

#endif // MAINWINDOW_H
