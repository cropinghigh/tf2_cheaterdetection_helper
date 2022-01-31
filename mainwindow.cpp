#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QFileDialog>

UpdaterThreadWorker::UpdaterThreadWorker(QStandardItemModel* retTableElements) {
    tableElements = retTableElements;
    QStringList headerLabels {"Nick                      ", "Time playing", "Steam reg.year", "Country", "Total play hours", "Steam lvl"};
    tableElements->clear();
    tableElements->setHorizontalHeaderLabels(headerLabels);
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

inline bool space(char c){
    return c == '\n';
}

inline bool notspace(char c){
    return c != '\n';
}

//break a sentence into words(by \n)
std::vector<std::string> split(const std::string& s){
    typedef std::string::const_iterator iter;
    std::vector<std::string> ret;
    iter i = s.begin();
    while(i!=s.end()){
        i = std::find_if(i, s.end(), notspace); // find the beginning of a word
        iter j= std::find_if(i, s.end(), space); // find the end of the same word
        if(i!=s.end()){
            ret.push_back(std::string(i, j)); //insert the word into vector
            i = j; // repeat 1,2,3 on the rest of the line.
        }
    }
    return ret;
}

bool UpdaterThreadWorker::processStatusOutput(const std::string status) {
    std::smatch matchA;
    std::smatch matchB;
    std::smatch matchC;
    std::regex users_rgx(
    "# userid name                uniqueid            connected ping loss state(?:  adr|)((?:\n#.*)+)"
                );
    std::regex addr_rgx(
    "udp\\/ip  : (.*)"
                );

    bool ret = false;
    if(std::regex_search(status.begin(), status.end(), matchA, addr_rgx) && std::regex_search(status.begin(), status.end(), matchB, users_rgx)) {
        std::string addr = matchA[1];
        lastConn = QString::fromStdString(addr);
        //std::string users = std::string(matchB[1]);
        std::string users = status;
        //users = users.substr(1, users.size()) + "\n";
        std::regex user_rgx("# +(.*?) +\"(.*?)\" +(.*?) +((?:\\:|\\d)+).*");
        std::list<std::string> parsed_users;

        if(users.size() > 0) {
            QList<QList<QStandardItem*>> tableElementsTemp;
            std::vector<std::string> users_s = split(users);
            for(int i = 0; i < users_s.size(); i++) {
                const std::string line = users_s[i];
                if (std::regex_search(line.begin(), line.end(), matchC, user_rgx)) {
                    std::string username = matchC[2];
                    std::string steamid64 = steamidto64(matchC[3]);
                    if((std::find(parsed_users.begin(), parsed_users.end(), username) != parsed_users.end())) {
                        continue;
                    }
                    parsed_users.push_back(username);
                    if(steamid64 != "err") {
                        std::string playtime = matchC[4];
                        steamUser user;
                        if(steamUsersCache.contains(steamid64) && ((steamUsersCache[steamid64].timestamp - time(NULL)) < 1800000L)) {
                            user = steamUsersCache[steamid64];
                        } else {
                            user = getUserData(steamid64, username);
                            steamUsersCache.remove(steamid64);
                            steamUsersCache.insert(steamid64, user);
                        }
                        tm creation_tm = *localtime(&user.created);
                        QList<QStandardItem*> row {
                            new QStandardItem(QString::fromStdString(user.tfname)),
                            new QStandardItem(QString::fromStdString(playtime)),
                            new QStandardItem(QString::number((creation_tm.tm_year + 1900)) == "1970" ? "-" : QString::number((creation_tm.tm_year + 1900))),
                            new QStandardItem(QString::fromStdString(user.country)),
                            new QStandardItem(QString::number(user.playhours) == "-1" ? "-" : QString::number(user.playhours)),
                            new QStandardItem(QString::number(user.steamLevel) == "-1" ? "-" : QString::number(user.steamLevel))};
                        bool suspicious = false;
                        if(user.name != user.tfname) {
                            suspicious = true;
                            QList<QStandardItem*> subrow {
                                new QStandardItem("Nick mismatch:"),
                                new QStandardItem("steam "),
                                new QStandardItem("\"" + QString::fromStdString(user.name) + "\","),
                                new QStandardItem("tf"),
                                new QStandardItem("\"" + QString::fromStdString(user.tfname) + "\"")
                            };
                            row[0]->setChild(0, 0, subrow[0]);
                            row[0]->setChild(0, 1, subrow[1]);
                            row[0]->setChild(0, 2, subrow[2]);
                            row[0]->setChild(0, 3, subrow[3]);
                            row[0]->setChild(0, 4, subrow[4]);
                        }
                        if(user.configured != 1) {
                            suspicious = true;
                            QList<QStandardItem*> subrow {
                                new QStandardItem("Profile is"),
                                new QStandardItem("not"),
                                new QStandardItem("configured")
                            };
                            row[0]->setChild(0, 0, subrow[0]);
                            row[0]->setChild(0, 1, subrow[1]);
                            row[0]->setChild(0, 2, subrow[2]);
                        }
                        if(user.visibility != 5 && user.visibility != 4 && user.visibility != 3) {
                            suspicious = true;
                            QList<QStandardItem*> subrow {
                                new QStandardItem("Profile is"),
                                new QStandardItem("not"),
                                new QStandardItem("public(" +
                                ((user.visibility == 1) ? QString("private") : QString("for friends")) + ")")
                            };
                            row[0]->setChild(0, 0, subrow[0]);
                            row[0]->setChild(0, 1, subrow[1]);
                            row[0]->setChild(0, 2, subrow[2]);
                        }
                        tableElementsTemp.append(row);
                    } else {
                        printf("ERROR: invalid steamid %s", steamid64.c_str());
                    }
                }
            }
            updateTime();
            if(tableElements->rowCount() > 0)
                tableElements->removeRows(0, tableElements->rowCount());
            foreach(auto row, tableElementsTemp) {
                tableElements->appendRow(row);
            }
            emit updateTable();
        }
        emit updateStatus("RCON connected", lastUpdate, lastConn);
        ret = true;
    } else {
        //tableElements->clear();
        emit updateStatus("RCON connected", lastUpdate, lastConn);
        emit updateTable();
        ret = false;
    }
    return ret;
}

void UpdaterThreadWorker::run() {
    while(!this->isInterruptionRequested()) {
        main_mtx.try_lock();
        srcon rconcli("127.0.0.1", 27015, rconpass.toStdString(), 1);
        if(rconcli.get_connected()) {
            const std::string status_data = rconcli.send("status");
            if(status_data == "Sending failed!") {
                emit updateStatus("RCON disconnected", lastUpdate, lastConn);
            } else if(status_data == "") {
                logfile_str.seekg( 0, std::ios::end );
                int curr_fsize = logfile_str.tellg();
                if(curr_fsize < lastPosition) {
                    lastPosition = curr_fsize;
                    logfile_buffer = "";
                } else if(curr_fsize > lastPosition) {
                    logfile_str.clear();
                    logfile_str.seekg(lastPosition);
                    int new_size = curr_fsize - lastPosition;
                    char buffer[new_size];
                    logfile_str.read(buffer, new_size);
                    logfile_buffer += buffer;
                    if(processStatusOutput(logfile_buffer)) {
                        logfile_buffer = "";
                    }
                    lastPosition = curr_fsize;
                } else {
                    emit updateStatus("RCON connected", lastUpdate, lastConn);
                }
            } else {
                processStatusOutput(status_data);
            }
        } else {
            emit updateStatus("RCON disconnected", lastUpdate, lastConn);
        }
        main_mtx.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
}

void UpdaterThreadWorker::updateTime() {
    time_t     now = time(0);
    struct tm  tstruct = *localtime(&now);
    char buffer[80];
    strftime(buffer,sizeof(buffer),"%H:%M:%S", &tstruct);
    lastUpdate = QString(buffer);
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

void UpdaterThreadWorker::on_logfile_update(const QString& retLogfile) {
    main_mtx.try_lock();
    logfile = retLogfile;
    logfile_str.close();
    logfile_str = std::ifstream(logfile.toStdString());
    if(!logfile_str.is_open()) {
        printf("ERROR: file %s can't be open", logfile.toStdString().c_str());
    }
    logfile_str.seekg( 0, std::ios::end );
    lastPosition = logfile_str.tellg();
    logfile_buffer = "";
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
    logfile = settings.value("logfile", "/").toString();
    ui->lineEdit->setText(rconpass);
    ui->lineEdit_2->setText(apiKey);
    ui->label_7->setText(logfile);
    ui->treeView->setModel(&tableModel);
    updateTable();
    connect(&utw, &UpdaterThreadWorker::updateTable, this, &MainWindow::on_table_update);
    connect(&utw, &UpdaterThreadWorker::updateStatus, this, &MainWindow::on_status_update);
    utw.on_rconpass_update(rconpass);
    utw.on_apikey_update(apiKey);
    utw.on_logfile_update(logfile);
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
    for(int i = 0; i < tableModel.columnCount(); i++)
            ui->treeView->resizeColumnToContents(i);
    ui->treeView->expandAll();
}


void MainWindow::on_lineEdit_2_textChanged(const QString &arg1)
{
    apiKey = arg1;
    utw.on_apikey_update(apiKey);
    settings.setValue("apikey", apiKey);
    settings.sync();
}


void MainWindow::on_pushButton_clicked()
{
    logfile = QFileDialog::getOpenFileName(this, tr("Open console.log file"),logfile, tr("Console log (console.log)"));
    ui->label_7->setText(logfile);
    utw.on_logfile_update(logfile);
}

