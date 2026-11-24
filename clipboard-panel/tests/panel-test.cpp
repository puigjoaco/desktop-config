#include "panel.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QScrollBar>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QProcess>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
using namespace Clip;
static QVector<Item> examples() {
    QVector<Item> entries;auto text=[&](QString id,double t,QString value){Item i;i.id=id;i.added=t;i.text=value;i.payloads["text/plain"]=value.toUtf8();entries<<i;};
    text("g",7,"Nos vemos el jueves a las 10:30.");
    Item shot;shot.id="f";shot.added=6;QImage image(1920,1080,QImage::Format_RGB32);image.fill(QColor(232,235,238));QPainter p(&image);p.fillRect(0,0,1920,105,QColor(210,214,220));p.fillRect(0,105,410,975,QColor(225,229,233));p.fillRect(510,200,810,65,QColor(48,54,61));p.fillRect(510,470,590,390,QColor(159,211,238));p.fillRect(1170,470,590,390,Qt::white);p.end();QByteArray png;QBuffer b(&png);b.open(QIODevice::WriteOnly);image.save(&b,"PNG");shot.payloads["image/png"]=png;entries<<shot;
    text("e",5,"www.ejemplo.com/recetas/pan-casero-sin-amasado-paso-a-paso");
    text("d",4,"Para el pan casero: mezclar 500 g de harina, 350 ml de agua tibia, 10 g de sal y una pizca de levadura. Dejar reposar toda la noche tapado, darle forma por la mañana y hornear 45 minutos a 230 °C en una olla con tapa.");
    text("c",3,"Av. Las Flores 1234, depto. 5B");entries.last().pinned=true;
    text("b",2,"Hola Marta,\nte paso el presupuesto actualizado.");
    text("a",1,"Texto más antiguo");return entries;
}
class PanelTest:public QObject {
    Q_OBJECT
private slots:
    void navigationAndChoose() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();
        auto *list=panel.findChild<QListWidget*>("history");QSignalSpy choose(&panel,&Panel::chosen),dismiss(&panel,&Panel::dismissRequested);
        QCOMPARE(panel.currentId(),QString("g"));QTest::keyClick(list,Qt::Key_Down);QCOMPARE(panel.currentId(),QString("f"));
        QTest::keyClick(list,Qt::Key_Return);QCOMPARE(choose.size(),1);QCOMPARE(choose[0][0].toString(),QString("f"));QCOMPARE(history.items().first().id,QString("g"));
        QTest::keyClicks(list,"p");auto *search=panel.findChild<QLineEdit*>("filter");QTest::keyClicks(search,"an");QCOMPARE(panel.filter(),QString("pan"));QCOMPARE(list->count(),2);
        QTest::keyClick(search,Qt::Key_Escape);QVERIFY(panel.filter().isEmpty());QCOMPARE(dismiss.size(),0);
        QTest::keyClick(list,Qt::Key_Escape);QCOMPARE(dismiss.size(),1);QVERIFY(dismiss[0][0].toBool());
        panel.prepareOpen();QTest::keyClick(list,Qt::Key_P,Qt::ControlModifier);QVERIFY(history.items().first().pinned);QTest::keyClick(list,Qt::Key_Delete);QCOMPARE(list->count(),6);
    }
    void sizePersistenceAndScreenClamping() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));
        const auto config=dir.filePath("panelrc");
        {
            Panel panel(&history,config);panel.prepareOpen(QRect(0,0,1600,1000));panel.show();QApplication::processEvents();
            QCOMPARE(panel.width(),560);QVERIFY(panel.height()<=760);QVERIFY(!QSettings(config,QSettings::IniFormat).contains("Window/size"));
            panel.resize(800,700);QApplication::processEvents();QCOMPARE(panel.size(),QSize(800,700));
            panel.findChild<QLineEdit*>("filter")->setText("no-existe");QApplication::processEvents();QCOMPARE(panel.size(),QSize(800,700));
            panel.hide();panel.prepareOpen(QRect(0,0,1600,1000));panel.show();QApplication::processEvents();QCOMPARE(panel.size(),QSize(800,700));
        }
        Panel restored(&history,config);restored.prepareOpen(QRect(1600,0,500,400));restored.show();QApplication::processEvents();QCOMPARE(restored.size(),QSize(500,400));
        QCOMPARE(QSettings(config,QSettings::IniFormat).value("Window/size").toSize(),QSize(800,700));
        restored.hide();restored.prepareOpen(QRect(0,0,1600,1000));restored.show();QApplication::processEvents();QCOMPARE(restored.size(),QSize(800,700));
        restored.resize(100,100);QApplication::processEvents();QCOMPARE(restored.size(),QSize(360,240));
        restored.hide();restored.prepareOpen(QRect(0,0,320,200));restored.show();QApplication::processEvents();QCOMPARE(restored.size(),QSize(320,200));
    }
    void responsiveCardHeightsAndResizeCursors() {
        QTemporaryDir dir;History history(dir.path());auto entries=examples();entries[0].text=QString(110,'W');entries[0].payloads["text/plain"]=entries[0].text.toUtf8();QVERIFY(history.merge(entries));
        Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen(QRect(0,0,1600,1000));panel.show();QApplication::processEvents();
        auto *list=panel.findChild<QListWidget*>("history");
        panel.resize(420,400);QApplication::processEvents();QApplication::processEvents();
        const int narrowText=list->item(0)->sizeHint().height(),narrowImage=list->item(1)->sizeHint().height();
        QCOMPARE(list->item(0)->sizeHint().width(),list->viewport()->width());QVERIFY(list->horizontalScrollBar()->maximum()==0);
        panel.resize(1200,700);QApplication::processEvents();QApplication::processEvents();
        QVERIFY(list->item(0)->sizeHint().height()<narrowText);QVERIFY(list->item(1)->sizeHint().height()>narrowImage);QVERIFY(list->item(1)->sizeHint().height()<=226);
        QCOMPARE(list->item(0)->sizeHint().width(),list->viewport()->width());
        for(const auto &[point,cursor]:QList<QPair<QPoint,Qt::CursorShape>>{{{2,2},Qt::SizeFDiagCursor},{{panel.width()-2,2},Qt::SizeBDiagCursor},{{2,100},Qt::SizeHorCursor},{{100,2},Qt::SizeVerCursor},{{100,100},Qt::ArrowCursor}}) {
            QTest::mouseMove(&panel,point);QCOMPARE(panel.cursor().shape(),cursor);
        }
    }
    void resizingBackToOpeningSizeIsPersisted() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));
        const auto config=dir.filePath("panelrc");QSize opening;
        {
            Panel panel(&history,config);panel.prepareOpen(QRect(0,0,1600,1000));panel.show();QApplication::processEvents();
            opening=panel.size();QVERIFY(!QSettings(config,QSettings::IniFormat).contains("Window/size"));
            panel.resize(800,700);QApplication::processEvents();
            QCOMPARE(QSettings(config,QSettings::IniFormat).value("Window/size").toSize(),QSize(800,700));
            panel.resize(opening);QApplication::processEvents();
            QCOMPARE(QSettings(config,QSettings::IniFormat).value("Window/size").toSize(),opening);
            panel.hide();panel.prepareOpen(QRect(0,0,1600,1000));panel.show();QApplication::processEvents();QCOMPARE(panel.size(),opening);
        }
        Panel restored(&history,config);restored.prepareOpen(QRect(0,0,1600,1000));restored.show();QApplication::processEvents();QCOMPARE(restored.size(),opening);
    }
    void clearAuthorization_data() {
        QTest::addColumn<int>("result");
        QTest::newRow("authorized")<<0;QTest::newRow("denied")<<1;QTest::newRow("dismissed")<<126;
        QTest::newRow("error")<<127;QTest::newRow("unavailable")<<-1;QTest::newRow("crashed")<<-2;
    }
    void clearAuthorization() {
        QFETCH(int,result);QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));
        const auto program=dir.filePath("pkcheck-fake");
        if(result!=-1) {
            QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nsleep 0.1\n");
            script.write(result==-2?QByteArray("kill -KILL $$\n"):QByteArray("exit ")+QByteArray::number(result)+"\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        }
        Panel panel(&history,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();
        QSignalSpy dismiss(&panel,&Panel::dismissRequested);auto *button=panel.findChild<QPushButton*>("clearAll");
        QTest::keyClick(button,Qt::Key_Return);QCOMPARE(history.items().size(),7);QVERIFY(button->isEnabled());
        QTest::mouseClick(button,Qt::LeftButton);QVERIFY(!button->isEnabled());
        QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&panel,&deactivate);QCOMPARE(dismiss.size(),0);QCOMPARE(history.items().size(),7);
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(history.items().size(),result==0?1:7);QVERIFY(panel.isVisible());QCOMPARE(dismiss.size(),0);
        if(result==0)QVERIFY(history.items().first().pinned);
    }
    void escapeCancelsAuthorization() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));const auto program=dir.filePath("pkcheck-fake");
        QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nexec sleep 10\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        Panel panel(&history,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();QSignalSpy dismiss(&panel,&Panel::dismissRequested);
        auto *button=panel.findChild<QPushButton*>("clearAll");QTest::mouseClick(button,Qt::LeftButton);QVERIFY(!button->isEnabled());QTest::keyClick(&panel,Qt::Key_Escape);QVERIFY(button->isEnabled());QCOMPARE(history.items().size(),7);QCOMPARE(dismiss.size(),0);
        QTRY_VERIFY(qApp->findChildren<QProcess*>().isEmpty());
    }
    void offscreenRenders() {
        // Rendering is intentionally unavailable on the user's real desktop session.
        QCOMPARE(QGuiApplication::platformName(),QString("offscreen"));
        const QString output=qEnvironmentVariable("CLIPBOARD_PANEL_RENDER_DIR");if(output.isEmpty())QSKIP("Set CLIPBOARD_PANEL_RENDER_DIR to export synthetic PNGs");
        // Offscreen Qt does not load Plasma's platform theme. Read only KDE color roles
        // for the render fixture, so the comparison uses the user's current palette.
        QSettings kde(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/kdeglobals",QSettings::IniFormat);
        QPalette palette=qApp->palette();
        auto color=[&](QString group,QString key,QColor fallback){const auto rgb=kde.value(group+'/'+key).toStringList();if(rgb.size()!=3)return fallback;return QColor(rgb[0].toInt(),rgb[1].toInt(),rgb[2].toInt());};
        const QList<std::tuple<QPalette::ColorRole,QString,QString>> roles{
            {QPalette::Window,"Colors:Window","BackgroundNormal"},{QPalette::WindowText,"Colors:Window","ForegroundNormal"},
            {QPalette::Text,"Colors:View","ForegroundNormal"},{QPalette::Base,"Colors:View","BackgroundNormal"},
            {QPalette::Button,"Colors:Button","BackgroundNormal"},{QPalette::ButtonText,"Colors:Button","ForegroundNormal"},
            {QPalette::Highlight,"Colors:Selection","BackgroundNormal"},{QPalette::HighlightedText,"Colors:Selection","ForegroundNormal"},
            {QPalette::Link,"Colors:Window","ForegroundLink"}};
        for(const auto &[role,group,key]:roles)palette.setColor(role,color(group,key,palette.color(role)));
        palette.setColor(QPalette::Disabled,QPalette::Text,color("Colors:Window","ForegroundInactive",palette.color(QPalette::Disabled,QPalette::Text)));
        qApp->setPalette(palette);
        QVERIFY(QDir().mkpath(output));QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));QVERIFY(history.choose("f"));Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();
        auto render=[&](const QString &name){QApplication::processEvents();QImage image(panel.size()*2,QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(2);image.fill(Qt::transparent);panel.render(&image);QVERIFY(image.save(QDir(output).filePath(name+".png")));};
        panel.prepareOpen(QRect(0,0,1600,1000));panel.resize(420,400);render("resize-420x400");
        panel.resize(800,700);render("resize-800x700");
        render("normal-selected");auto *list=panel.findChild<QListWidget*>("history");list->clearSelection();render("normal-hoverless");list->setCurrentRow(1);render("selected-image");
        QTest::keyClicks(list,"p");auto *search=panel.findChild<QLineEdit*>("filter");QTest::keyClicks(search,"an");render("filter");search->setText("no-existe");render("filter-empty");search->clear();panel.prepareOpen();
        for(const auto &item:history.items())history.remove(item.id);panel.refresh(true);render("empty");
    }
};
int main(int argc,char **argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);
    PanelTest test;return QTest::qExec(&test,argc,argv);
}
#include "panel-test.moc"
