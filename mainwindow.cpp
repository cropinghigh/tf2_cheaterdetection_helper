#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QFileDialog>

UpdaterThreadWorker::UpdaterThreadWorker(QStandardItemModel* retTableElements) {
    tableElements = retTableElements;
}

UpdaterThreadWorker::~UpdaterThreadWorker() {

}

std::string steamidto64(const std::string steamid) {
    std::string ret = "err";
    std::regex rgx64("\\d{17,}");
    std::regex rgx32("STEAM_0\\:(\\d+)\\:(\\d+)");
    std::regex rgx3("\\[U:1:(\\d+)\\]");
    std::smatch match;
    if (std::regex_search(steamid.begin(), steamid.end(), match, rgx3)) {
        std::string steam3_s = match[1];
        unsigned long steam3 = std::stol(steam3_s);
        unsigned long steam64 = 76561197960265728L + steam3;
        ret = std::to_string(steam64);
    } else if (std::regex_search(steamid.begin(), steamid.end(), match, rgx64)) {
        ret = match[0];
    } else if (std::regex_search(steamid.begin(), steamid.end(), match, rgx32)) {
        std::string steam32A_s = match[1];
        std::string steam32B_s = match[2];
        unsigned long steam32A = std::stol(steam32A_s);
        unsigned long steam32B = std::stol(steam32B_s);
        unsigned long steam64 = ((steam32B * 2) + 76561197960265728L) + steam32A;
        ret = std::to_string(steam64);
    }

    return ret;
}

steamUser UpdaterThreadWorker::getUserData(std::string steamid64, std::string name) {
    steamUser ret;
    std::regex key_rgx("(?:[a-zA-Z]|\\d){32}");
    if(apiKey.size() != 0 && (std::regex_match(apiKey.toStdString(), key_rgx))) {
        auto res = httpCli.Get(("/ISteamUser/GetPlayerSummaries/v2/?key=" + apiKey + "&steamids=" + QString::fromStdString(steamid64)).toStdString().c_str());
        if(res->status != 200) {
            printf("ERROR: incorrect response(%d, %s)", res->status, res->body.c_str());
            goto error;
        }
        json responseJson = json::parse(res->body)["response"];
        if(responseJson["players"][0]["steamid"] != steamid64) {
            printf("ERROR: incorrect steamid(%s). wanted(%s)", std::string(responseJson["players"][0]["steamid"]).c_str(), steamid64.c_str());
            goto error;
        }
        ret.name = responseJson["players"][0]["personaname"];
        ret.tfname = name;
        ret.timestamp = time(NULL);
        ret.visibility = responseJson["players"][0]["communityvisibilitystate"];
        ret.configured = (responseJson["players"][0]["profilestate"] == 1);
        if(responseJson["players"][0].contains("timecreated")) {
            ret.created = responseJson["players"][0]["timecreated"];
        } else {
            ret.created = 0;
        }
        if(responseJson["players"][0].contains("loccountrycode")) {
            ret.country = std::string(responseJson["players"][0]["loccountrycode"]);
        } else {
            ret.country = "-";
        }
        ret.playhours = -1;
        ret.steamLevel = -1;

        res = httpCli.Get(("/IPlayerService/GetOwnedGames/v1/?key=" + apiKey + "&include_played_free_games=true&steamid=" + QString::fromStdString(steamid64)).toStdString().c_str());
        if(res->status != 200) {
            goto fin;
        }
        responseJson = json::parse(res->body)["response"];
        if(responseJson.contains("games")) {
            for(int i = 0; i < responseJson["games"].size(); i++) {
                if(responseJson["games"][i]["appid"] == 440) {
                    ret.playhours = ((int)responseJson["games"][i]["playtime_forever"]) / 60;
                }
            }
        }
        res = httpCli.Get(("/IPlayerService/GetSteamLevel/v1/?key=" + apiKey + "&steamid=" + QString::fromStdString(steamid64)).toStdString().c_str());
        if(res->status != 200) {
            goto fin;
        }
        responseJson = json::parse(res->body)["response"];
        if(responseJson.contains("player_level")) {
            ret.steamLevel = responseJson["player_level"];
        }
fin:
        return ret;
    }
    printf("ERROR: Key is incorrect");
error:
    ret.timestamp = 0;
    ret.visibility = 0;
    ret.tfname = "API ERROR";
    ret.name = "API ERROR!";
    ret.created = 0;
    ret.country = "NULL";
    ret.configured = 0;
    ret.playhours = 0;
    ret.steamLevel = 0;
    return ret;
}

void UpdaterThreadWorker::run() {
    while(!this->isInterruptionRequested()) {
        main_mtx.try_lock();
        srcon rconcli("127.0.0.1", 27015, rconpass.toStdString(), 1);
        if(rconcli.get_connected()) {
            const std::string status_data = rconcli.send("status");
            if(status_data != "Sending failed!") {
                time_t     now = time(0);
                struct tm  tstruct = *localtime(&now);
                char buffer[80];
                strftime(buffer,sizeof(buffer),"%H:%M:%S", &tstruct);
                lastUpdate = QString(buffer);
                if(status_data == "") {
                    tableElements->clear();
                    emit updateStatus("RCON connected", lastUpdate, "None");
                    emit updateTable();
                } else {
                    std::smatch match;
                    std::regex main_rgx(
                    "hostname: (.*)\nversion : (.*)\nudp\\/ip  : (.*)\naccount : (.*)\nmap     : (.*)\ntags    : (.*)\nplayers : (.*)\nedicts  : (.*)\n# userid name                uniqueid            connected ping loss state  adr\n((?:.|\n)*)"
                                );
                    if(std::regex_search(status_data.begin(), status_data.end(), match, main_rgx)) {
                        //std::string addr = status_data.substr(status_data.find("udp/ip  : "), status_data.find("account :"));
                        //std::string users = status_data.substr(status_data.find("# userid name                uniqueid            connected ping loss state  adr"), status_data.size());
                        std::string addr = match[3];
                        std::string users = match[9];
                        std::regex user_rgx("# *(.) *\"(.*)\" *(.*?) *(\\d\\d:\\d\\d) *\\d+ *\\d+.*");

                        int last_match = 0;
                        if(users.size() > 0) {
                            QList<QStandardItem *> tableElementsTemp;
                            for(int i = 0; i < std::count(users.begin(), users.end(), '\n') + 1; i++) {
                                const std::string line = users.substr(last_match, users.find('\n', last_match));
                                if (std::regex_search(line.begin(), line.end(), match, user_rgx)) {
                                    std::string username = match[2];
                                    std::string steamid64 = steamidto64(match[3]);
                                    if(steamid64 != "err") {
                                        std::string playtime = match[4];
                                        steamUser user;
                                        if(steamUsersCache.contains(steamid64) && ((steamUsersCache[steamid64].timestamp - time(NULL)) < 1800000L)) {
                                            user = steamUsersCache[steamid64];
                                        } else {
                                            user = getUserData(steamid64, username);
                                            steamUsersCache.remove(steamid64);
                                            steamUsersCache.insert(steamid64, user);
                                        }
                                        tm creation_tm = *localtime(&user.created);
                                        QStandardItem* userbase = new QStandardItem(
                                                    QString::fromStdString(user.tfname) + " | " +
                                                    QString::fromStdString(playtime) + " | Steam r.y: " +
                                                    QString::number(creation_tm.tm_year + 1900) + " | " +
                                                    QString::fromStdString(user.country) + " | h.p:" +
                                                    QString::number(user.playhours) + " | s.l:" +
                                                    QString::number(user.steamLevel));
                                        bool suspicious = false;
                                        if(user.name != user.tfname) {
                                            suspicious = true;
                                            userbase->appendRow(new QStandardItem("Nick mismatch: steam " + QString::fromStdString(user.name) + ", tf " + QString::fromStdString(user.tfname)));
                                        }
                                        if(user.configured != 1) {
                                            suspicious = true;
                                            userbase->appendRow(new QStandardItem("Profile is not configured"));
                                        }
                                        if(user.visibility != 5 && user.visibility != 4 && user.visibility != 3) {
                                            suspicious = true;
                                            userbase->appendRow(new QStandardItem("Profile is not public(" +
                                                                                 ((user.visibility == 1) ? QString("private") : QString("for friends")) + ")"));
                                        }
                                        tableElementsTemp.append(userbase);
                                    } else {
                                        printf("ERROR: invalid steamid %s", steamid64.c_str());
                                    }
                                }
                            }
                            tableElements->clear();
                            foreach(auto row, tableElementsTemp) {
                                tableElements->appendRow(row);
                            }
                        }
                        emit updateStatus("RCON connected", lastUpdate, QString::fromStdString(addr));
                    }

                    emit updateTable();
                }
                main_mtx.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }
        emit updateStatus("RCON disconnected", lastUpdate, "None");
        main_mtx.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void UpdaterThreadWorker::on_rconpass_update(const QString &retRconPass) {
    main_mtx.try_lock();
    rconpass = retRconPass;
    main_mtx.unlock();
}

void UpdaterThreadWorker::on_apikey_update(const QString& retApiKey) {
    main_mtx.try_lock();
    apiKey = retApiKey;
    main_mtx.unlock();
}


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      settings("Indir", "TF2CheatDetectionHelper"),
      utw(&tableModel)
{
    ui->setupUi(this);
    ui->centralwidget->setLayout(ui->gridLayout);
    ui->centralwidget->setWindowTitle("TF2 cheater detection helper");
    rconpass = settings.value("rconpass", "").toString();
    apiKey = settings.value("apikey", "").toString();
    ui->lineEdit->setText(rconpass);
    ui->lineEdit_2->setText(apiKey);
    tableModel.clear();
    ui->treeView->setModel(&tableModel);
    updateTable();
    connect(&utw, &UpdaterThreadWorker::updateTable, this, &MainWindow::on_table_update);
    connect(&utw, &UpdaterThreadWorker::updateStatus, this, &MainWindow::on_status_update);
    utw.on_rconpass_update(rconpass);
    utw.on_apikey_update(apiKey);
    utw.start();
}

MainWindow::~MainWindow()
{
    if(utw.isRunning()) {
        utw.requestInterruption();
        utw.wait();
    }
    delete ui;
}

void MainWindow::on_lineEdit_textChanged(const QString &arg1)
{
    rconpass = arg1;
    utw.on_rconpass_update(rconpass);
    settings.setValue("rconpass", rconpass);
    settings.sync();
}

void MainWindow::on_status_update(const QString& status, const QString& lastUpdate, const QString& connectedTo) {
    ui->label_2->setText(status);
    ui->label_4->setText("Last update: " + lastUpdate);
    ui->label_5->setText("Connected to: " + connectedTo);
}

void MainWindow::on_table_update() {
    updateTable();
}

void MainWindow::updateTable() {
    //ui->treeView->setModel(&tableModel);
    ui->treeView->expandAll();
}


void MainWindow::on_lineEdit_2_textChanged(const QString &arg1)
{
    apiKey = arg1;
    utw.on_apikey_update(apiKey);
    settings.setValue("apikey", apiKey);
    settings.sync();
}

