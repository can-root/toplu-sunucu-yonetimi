#include <QApplication>
#include <QMainWindow>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFormLayout>
#include <QLineEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>

class HttpServer : public QTcpServer {
    Q_OBJECT

public:
    HttpServer(const QString &ip, quint16 port, const QString &belgeKok, QObject *parent = nullptr)
    : QTcpServer(parent), belgeKok(belgeKok) {
        listen(QHostAddress(ip), port);
    }

    QString sunucuAdi() const {
        return QString("%1:%2").arg(serverAddress().toString()).arg(serverPort());
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override {
        QTcpSocket *socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            delete socket;
            return;
        }
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            while (socket->canReadLine()) {
                QString satir = QString::fromUtf8(socket->readLine()).trimmed();
                if (satir.startsWith("GET")) {
                    istekIsle(socket, satir);
                    break;
                }
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }

private:
    void istekIsle(QTcpSocket *socket, const QString &istekSatiri) {
        QStringList parcalar = istekSatiri.split(" ");
        if (parcalar.size() < 2) return;

        QString yol = parcalar[1] == "/" ? "/index.html" : parcalar[1];
        QFile dosya(belgeKok + yol);
        if (dosya.exists() && dosya.open(QIODevice::ReadOnly)) {
            QByteArray icerik = dosya.readAll();
            socket->write("HTTP/1.1 200 OK\r\n");
            socket->write("Content-Type: text/html\r\n");
            socket->write("Content-Length: " + QByteArray::number(icerik.size()) + "\r\n");
            socket->write("\r\n");
            socket->write(icerik);
            dosya.close();
        } else {
            socket->write("HTTP/1.1 500 Internal Server Error\r\n");
            socket->write("Content-Type: text/plain\r\n");
            socket->write("\r\n");
            socket->write("Port bosaltildi.");
            socket->flush();
            socket->disconnectFromHost();
            close();
        }
        socket->flush();
        socket->disconnectFromHost();
    }

    QString belgeKok;
};

class AnaPencere : public QMainWindow {
    Q_OBJECT

public:
    AnaPencere(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Sunucu Yonetici");
        resize(400, 300);

        QWidget *merkezWidget = new QWidget(this);
        QVBoxLayout *duzen = new QVBoxLayout(merkezWidget);

        sunucuListeWidget = new QListWidget(this);
        duzen->addWidget(sunucuListeWidget);

        ekleButonu = new QPushButton("Sunucu Ekle", this);
        silButonu = new QPushButton("Sunucu Sil", this);

        duzen->addWidget(ekleButonu);
        duzen->addWidget(silButonu);

        setCentralWidget(merkezWidget);

        connect(ekleButonu, &QPushButton::clicked, this, &AnaPencere::sunucuEkle);
        connect(silButonu, &QPushButton::clicked, this, &AnaPencere::sunucuSil);

        sunucularıYukle();
        tumSunucularıBaslat();
    }

    ~AnaPencere() {
        tumSunucularıDurdur();
    }

private slots:
    void sunucuEkle() {
        QString ip, isim;
        int port;

        QDialog dialog(this);
        dialog.setWindowTitle("Sunucu Ekle");
        QFormLayout form(&dialog);

        QLineEdit *ipLineEdit = new QLineEdit(&dialog);
        ipLineEdit->setPlaceholderText("??.??.??.??");
        form.addRow("IP Adresi:", ipLineEdit);

        QLineEdit *portLineEdit = new QLineEdit(&dialog);
        portLineEdit->setPlaceholderText("Port (orn: 8080)");
        form.addRow("Port:", portLineEdit);

        QLineEdit *isimLineEdit = new QLineEdit(&dialog);
        form.addRow("Sunucu Adi:", isimLineEdit);

        QPushButton *ekleButon = new QPushButton("Ekle", &dialog);
        form.addRow(ekleButon);

        connect(ekleButon, &QPushButton::clicked, [&]() {
            ip = ipLineEdit->text();
            QString portString = portLineEdit->text();
            bool ok;
            port = portString.toUInt(&ok);
            isim = isimLineEdit->text();

            if (!ok || port < 1 || port > 65535) {
                QMessageBox::warning(this, "Hata", "Gecerli bir port numarasi girin (1-65535).");
                return;
            }

            dialog.accept();
        });

        if (dialog.exec() != QDialog::Accepted) return;

        for (const auto &sunucu : islemler) {
            if (sunucu->serverPort() == port && sunucu->serverAddress() == QHostAddress(ip)) {
                QMessageBox::warning(this, "Uyari", "Bu IP ve port kombinasyonu zaten kullaniliyor!");
                return;
            }
        }

        QString sunucuYolu = "sunucular/" + isim;
        QDir dir(sunucuYolu);

        if (!dir.exists()) {
            dir.mkpath(".");
            QFile indexFile(sunucuYolu + "/index.html");
            if (indexFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&indexFile);
                out << "<html><body><h1>Server: " << isim << "</h1></body></html>";
                indexFile.close();
            }
        } else {
            QMessageBox::warning(this, "Uyari", "Bu sunucu zaten mevcut!");
            return;
        }

        sunucuyuKaydet(ip, port, isim);
        sunucuBaslat(ip, port, sunucuYolu);
        sunucularıYukle();
    }

    void sunucuSil() {
        QListWidgetItem *item = sunucuListeWidget->currentItem();
        if (!item) return;

        QString sunucuBilgisi = item->text();
        QString sunucuAdi = sunucuBilgisi.split(" ")[2];
        sunucuDurdur(sunucuAdi);

        QDir dir("sunucular/" + sunucuAdi);
        if (dir.exists()) {
            dir.removeRecursively();
        }

        QFile file("sunucu.txt");
        if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QTextStream in(&file);
            QStringList satirlar;
            while (!in.atEnd()) {
                QString satir = in.readLine();
                if (!satir.contains(sunucuAdi)) {
                    satirlar << satir;
                }
            }
            file.resize(0);
            for (const QString &satir : satirlar) {
                in << satir << "\n";
            }
            file.close();
        }

        sunucularıYukle();
    }

private:
    void sunucularıYukle() {
        sunucuListeWidget->clear();
        QDir dir("sunucular");
        QStringList sunucular = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QFile file("sunucu.txt");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString satir = in.readLine();
                QStringList parcalar = satir.split(":");
                if (parcalar.size() == 3) {
                    sunucuListeWidget->addItem(parcalar[0] + " " + parcalar[1] + " " + parcalar[2]);
                }
            }
            file.close();
        }
    }

    void sunucuyuKaydet(const QString &ip, int port, const QString &isim) {
        QFile file("sunucu.txt");
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << ip << ":" << port << ":" << isim << "\n";
            file.close();
        }
    }

    void sunucuDurdur(const QString &isim) {
        for (int i = 0; i < islemler.size(); ++i) {
            if (islemler[i]->sunucuAdi() == isim) {
                islemler[i]->close();
                delete islemler[i];
                islemler.removeAt(i);
                break;
            }
        }
    }

    void tumSunucularıDurdur() {
        for (auto sunucu : islemler) {
            sunucu->close();
            delete sunucu;
        }
        islemler.clear();
    }

    void sunucuBaslat(const QString &ip, int port, const QString &belgeKok) {
        HttpServer *sunucu = new HttpServer(ip, port, belgeKok);
        if (sunucu->isListening()) {
            islemler.append(sunucu);
        } else {
            QMessageBox::warning(this, "Hata", "Sunucu baslatilamadi.");
            delete sunucu;
        }
    }

    void tumSunucularıBaslat() {
        QFile file("sunucu.txt");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString satir = in.readLine();
                QStringList parcalar = satir.split(":");
                if (parcalar.size() == 3) {
                    QString ip = parcalar[0];
                    int port = parcalar[1].toInt();
                    QString isim = parcalar[2];
                    sunucuBaslat(ip, port, "sunucular/" + isim);
                }
            }
            file.close();
        }
    }

    QListWidget *sunucuListeWidget;
    QPushButton *ekleButonu;
    QPushButton *silButonu;
    QVector<HttpServer*> islemler;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    AnaPencere anaPencere;
    anaPencere.show();
    return app.exec();
}

#include "main.moc"
