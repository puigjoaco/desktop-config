#include "history.h"
#include <QBuffer>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>
using namespace Clip;
static Item textItem(QString id,double time,QString text={}) {
    Item item;item.id=id;item.added=time;item.text=text.isNull()?id:text;item.payloads.insert(QStringLiteral("text/plain"),item.text.toUtf8());return item;
}
static QStringList ids(const QVector<Item> &items) {QStringList out;for(const auto &i:items)out<<i.id;return out;}
class HistoryTest : public QObject {
    Q_OBJECT
private slots:
    void chooseNeverReorders() {
        QTemporaryDir dir;
        QVector<Item> original{textItem("a",1),textItem("b",2),textItem("c",3)};
        {
            History history(dir.path());QVERIFY(history.isOpen());QVERIFY(history.merge(original));
            const auto before=history.items();
            for(const auto &id:QStringList{"a","b","c","a","a"}) {
                auto mime=history.choose(id);QVERIFY(mime);QCOMPARE(mime->text(),id);
                QCOMPARE(ids(history.items()),QStringList({"c","b","a"}));
                for(int i=0;i<3;++i)QCOMPARE(history.items()[i].sequence,before[i].sequence);
            }
            original[0].added=9999; // Klipper eviction/re-copy of the same UUID cannot move it.
            QVERIFY(history.merge(original));QCOMPARE(ids(history.items()),QStringList({"c","b","a"}));
        }
        History reopened(dir.path());QCOMPARE(ids(reopened.items()),QStringList({"c","b","a"}));QCOMPARE(reopened.lastChosen(),QString("a"));
    }
    void latePayloadAndTies() {
        QTemporaryDir dir;History h(dir.path());QVERIFY(h.merge({textItem("z",3),textItem("b",2),textItem("a",2)}));
        QVERIFY(h.merge({textItem("late",1)}));QCOMPARE(ids(h.items()),QStringList({"z","b","a","late"}));
        QVERIFY(h.merge({textItem("new",4)}));QCOMPARE(h.items().first().id,QString("new"));
    }
    void pinsDeleteClearFilter() {
        QTemporaryDir dir;History h(dir.path());
        const auto a=textItem("a",1,"Pan casero"),b=textItem("b",2,"Hola Marta");
        QVERIFY(h.merge({a,b}));QCOMPARE(ids(h.items("PAN")),QStringList({"a"}));QVERIFY(h.items("xyz").isEmpty());
        QVERIFY(h.pin("a"));QCOMPARE(ids(h.items()),QStringList({"b","a"}));QVERIFY(h.items().last().pinned);
        QVERIFY(h.clear());QCOMPARE(ids(h.items()),QStringList({"a"}));QVERIFY(h.merge({a,b}));QCOMPARE(ids(h.items()),QStringList({"a"}));
        QVERIFY(h.remove("a"));QVERIFY(h.items().isEmpty());QVERIFY(!h.choose("a"));QVERIFY(h.merge({a,b}));QVERIFY(h.items().isEmpty());
        QVERIFY(h.knownIds().contains("b"));
    }
    void retentionSparesPinned() {
        QTemporaryDir dir;History h(dir.path());auto pin=textItem("pinned",.5);pin.pinned=true;QVector<Item> entries{pin};
        for(int i=0;i<105;++i)entries<<textItem(QString::number(i),i+1);
        QVERIFY(h.merge(entries));QCOMPARE(h.items().size(),101);QVERIFY(h.items().last().pinned);QVERIFY(h.merge(entries));QCOMPARE(h.items().size(),101);QVERIFY(!h.choose("0"));
        // Unpinning an old card must enforce the limit without another Klipper import.
        QVERIFY(h.pin("pinned"));QCOMPARE(h.items().size(),100);QVERIFY(!h.choose("pinned"));
        QCOMPARE(h.items().first().id,QString("104"));QCOMPARE(h.items().last().id,QString("5"));
        for(const auto &item:h.items())QVERIFY(h.choose(item.id));
    }
    void listingUsesMetadataAndBoundedPreviews() {
        QTemporaryDir dir;History h(dir.path());
        auto text=textItem("text",2,"Needle");
        text.payloads["application/x-large"]=QByteArray(8*1024*1024,'x');
        Item picture;picture.id="picture";picture.added=1;picture.pinned=true;
        QImage image(1920,1080,QImage::Format_RGB32);image.fill(Qt::blue);
        QByteArray png;QBuffer output(&png);QVERIFY(output.open(QIODevice::WriteOnly));QVERIFY(image.save(&output,"PNG"));picture.payloads["image/png"]=png;
        QVERIFY(!h.hasItems());QVERIFY(h.merge({text,picture}));QVERIFY(h.hasItems());
        for(const auto &item:h.items())QVERIFY(item.payloads.isEmpty());
        const auto filtered=h.items("NEEDLE");QCOMPARE(ids(filtered),QStringList({"text"}));QVERIFY(filtered[0].payloads.isEmpty());
        QCOMPARE(ids(h.items("captura")),QStringList({"picture"}));QVERIFY(h.items("missing").isEmpty());
        const auto preview=h.imagePreview("picture");QCOMPARE(preview.original,image.size());QVERIFY(!preview.thumbnail.isNull());
        QVERIFY(preview.thumbnail.width()<=1024);QVERIFY(preview.thumbnail.height()<=200);QVERIFY(h.imagePreview("text").thumbnail.isNull());
        auto chosen=h.choose("text");QVERIFY(chosen);QCOMPARE(chosen->data("application/x-large"),text.payloads["application/x-large"]);
        chosen=h.choose("picture");QVERIFY(chosen);QCOMPARE(chosen->data("image/png"),png);QCOMPARE(qvariant_cast<QImage>(chosen->imageData()),image);
        QVERIFY(h.remove("picture"));QVERIFY(h.imagePreview("picture").thumbnail.isNull());QVERIFY(h.clear());QVERIFY(!h.hasItems());
    }
    void failedCommitRollsBack_data() {
        QTest::addColumn<QString>("operation");
        for(const auto &operation:QStringList{"merge","pin","remove","clear"})QTest::newRow(qPrintable(operation))<<operation;
    }
    void failedCommitRollsBack() {
        QFETCH(QString,operation);QTemporaryDir dir;History h(dir.path());QVERIFY(h.merge({textItem("a",1)}));
        const auto act=[&] {
            if(operation=="merge")return h.merge({textItem("b",2)});
            if(operation=="pin")return h.pin("a");
            if(operation=="remove")return h.remove("a");
            return h.clear();
        };
        const QString connection="commit-lock";
        {
            auto reader=QSqlDatabase::addDatabase("QSQLITE",connection);reader.setDatabaseName(dir.filePath("history.sqlite"));QVERIFY(reader.open());
            QVERIFY(reader.transaction());QSqlQuery lock(reader);QVERIFY(lock.exec("SELECT * FROM items"));QVERIFY(lock.next());
            // A reader allows the write transaction to begin but blocks its COMMIT.
            QVERIFY(!act());QVERIFY(!h.error().isEmpty());
            QCOMPARE(ids(h.items()),QStringList({"a"}));QVERIFY(!h.items().first().pinned);
            lock.finish();QVERIFY(reader.rollback());reader.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(act()); // A failed COMMIT must not poison the next transaction.
        if(operation=="merge")QCOMPARE(ids(h.items()),QStringList({"b","a"}));
        else if(operation=="pin")QVERIFY(h.items().first().pinned);
        else QVERIFY(h.items().isEmpty());
    }
    void textCleanup() {
        QCOMPARE(cleanText(" \t Hola\t  Marta \r\n   texto  \r\n\r\n\r\n fin \n"),QString("Hola Marta\ntexto\n\nfin"));
        QVERIFY(isLink("https://example.test/path"));QVERIFY(isLink("www.example.test"));QVERIFY(!isLink("https://example.test two"));
    }
    void syntheticKlipperMimeSnapshot() {
        QTemporaryDir dir;const QString file=dir.filePath("history3.sqlite"),data=dir.filePath("data");
        const QString name="fixture";
        QImage image(31,17,QImage::Format_RGB32);image.fill(Qt::blue);QByteArray png;QBuffer out(&png);QVERIFY(out.open(QIODevice::WriteOnly));QVERIFY(image.save(&out,"PNG"));
        const QMap<QString,QByteArray> payloads{{"text/plain","example"},{"text/html","<b>example</b>"},{"text/uri-list","https://example.test/\r\n"},{"image/png",png},{"application/x-synthetic","\1\2\3"}};
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(file);QVERIFY(db.open());QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE main (uuid char(40) PRIMARY KEY, added_time REAL NOT NULL CHECK (added_time > 0), last_used_time REAL CHECK (last_used_time > 0), mimetypes TEXT NOT NULL, text NTEXT, starred BOOLEAN)"));
            QVERIFY(q.exec("CREATE TABLE aux (uuid char(40) NOT NULL, mimetype TEXT NOT NULL, data_uuid char(40) NOT NULL, PRIMARY KEY (uuid,mimetype))"));
            QVERIFY(q.exec("INSERT INTO main VALUES('uuid',12,99,'text/plain,image/png','example',1)"));
            QVERIFY(QDir().mkpath(data+"/uuid"));int i=0;
            for(auto it=payloads.cbegin();it!=payloads.cend();++it) {
                const QString dataId=QString::number(i++);QFile f(data+"/uuid/"+dataId);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(it.value()),it.value().size());f.close();
                q.prepare("INSERT INTO aux VALUES('uuid',?,?)");q.addBindValue(it.key());q.addBindValue(dataId);QVERIFY(q.exec());
            }
            QVERIFY(q.exec("INSERT INTO main VALUES('half',14,101,'text/plain,text/html','incomplete',0)"));
            QVERIFY(q.exec("INSERT INTO aux VALUES('half','text/plain','text')"));
            QVERIFY(QDir().mkpath(data+"/half"));QFile half(data+"/half/text");QVERIFY(half.open(QIODevice::WriteOnly));half.write("incomplete");half.close();
            QVERIFY(q.exec("INSERT INTO main VALUES('pending',13,100,'image/png','',0)"));QVERIFY(q.exec("INSERT INTO aux VALUES('pending','image/png','not-yet-written')"));
        }
        QSqlDatabase::removeDatabase(name);
        QFile source(file);QVERIFY(source.open(QIODevice::ReadOnly));const auto before=source.readAll();source.close();
        QString error;const auto entries=readKlipper(file,data,&error);QVERIFY(error.isEmpty());QCOMPARE(entries.size(),1);QCOMPARE(entries[0].payloads,payloads);QVERIFY(entries[0].pinned);
        auto mime=entries[0].mimeData();for(auto it=payloads.cbegin();it!=payloads.cend();++it)QCOMPARE(mime->data(it.key()),it.value());QCOMPARE(qvariant_cast<QImage>(mime->imageData()),image);
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),before);
        QTemporaryDir own;History history(own.path());QVERIFY(history.merge(entries));auto restored=history.choose("uuid");QCOMPARE(restored->data("text/html"),payloads["text/html"]);QCOMPARE(qvariant_cast<QImage>(restored->imageData()).size(),QSize(31,17));
        struct stat statbuf{};QCOMPARE(::stat(QFile::encodeName(own.path()).constData(),&statbuf),0);QCOMPARE(statbuf.st_mode&0777,mode_t(0700));
        QVERIFY(readKlipper(file,data,&error,history.knownIds()).isEmpty());QVERIFY(error.isEmpty());
        // The private store owns bytes, not paths into Klipper's disposable payloads.
        QVERIFY(QFile::remove(file));QVERIFY(QDir(data).removeRecursively());
        History reopened(own.path());auto surviving=reopened.choose("uuid");QVERIFY(surviving);
        for(auto it=payloads.cbegin();it!=payloads.cend();++it)QCOMPARE(surviving->data(it.key()),it.value());
        QCOMPARE(qvariant_cast<QImage>(surviving->imageData()),image);
    }
    void missingDbIsNotCreated() {
        QTemporaryDir dir;QString error;const auto missing=dir.filePath("missing.sqlite");QVERIFY(readKlipper(missing,dir.path(),&error).isEmpty());QVERIFY(!error.isEmpty());QVERIFY(!QFile::exists(missing));
    }
};
QTEST_GUILESS_MAIN(HistoryTest)
#include "history-test.moc"
