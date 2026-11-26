#include "panel.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QScrollBar>
#include <QScrollArea>
#include <QScopeGuard>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QProcess>
#include <QToolButton>
#include <QTimer>
#include <QLabel>
#include <QElapsedTimer>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleOptionSlider>
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
        auto *list=panel.findChild<QListWidget*>("history");auto *search=panel.findChild<QLineEdit*>("filter");QApplication::processEvents();QVERIFY(search->isVisible());QVERIFY(search->hasFocus());QSignalSpy choose(&panel,&Panel::chosen),dismiss(&panel,&Panel::dismissRequested);
        QCOMPARE(panel.currentId(),QString("g"));QTest::keyClick(search,Qt::Key_Down);QCOMPARE(panel.currentId(),QString("f"));
        QTest::keyClick(search,Qt::Key_Return);QCOMPARE(choose.size(),1);QCOMPARE(choose[0][0].toString(),QString("f"));QCOMPARE(history.items().first().id,QString("g"));
        QTest::keyClicks(search,"pan");QCOMPARE(panel.filter(),QString("pan"));QCOMPARE(list->count(),2);
        QTest::keyClick(search,Qt::Key_Escape);QVERIFY(panel.filter().isEmpty());QVERIFY(search->hasFocus());QCOMPARE(panel.currentId(),QString("g"));QCOMPARE(dismiss.size(),0);
        QTest::keyClick(search,Qt::Key_Escape);QCOMPARE(dismiss.size(),1);QVERIFY(dismiss[0][0].toBool());
        panel.prepareOpen();QTest::keyClick(search,Qt::Key_P,Qt::ControlModifier);QVERIFY(history.items().first().pinned);QTest::keyClick(search,Qt::Key_Delete);QCOMPARE(list->count(),6);
    }
    void searchEditingAndNavigation() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();panel.resize(560,400);QApplication::processEvents();
        auto *search=panel.findChild<QLineEdit*>("filter");auto *list=panel.findChild<QListWidget*>("history");
        QTest::keyClick(search,Qt::Key_Up);QCOMPARE(panel.currentId(),QString("g"));
        QTest::keyClick(search,Qt::Key_End);QCOMPARE(panel.currentId(),QString("a"));QVERIFY(list->verticalScrollBar()->value()>0);
        QTest::keyClick(search,Qt::Key_Down);QCOMPARE(panel.currentId(),QString("a"));
        QTest::keyClick(search,Qt::Key_Home);QCOMPARE(panel.currentId(),QString("g"));QCOMPARE(list->verticalScrollBar()->value(),0);
        QTest::keyClick(search,Qt::Key_PageDown);QVERIFY(list->currentRow()>0);
        QTest::keyClick(search,Qt::Key_PageUp);QCOMPARE(panel.currentId(),QString("g"));
        QTest::keyClick(search,Qt::Key_Tab);QVERIFY(search->hasFocus());QTest::keyClick(search,Qt::Key_Backtab);QVERIFY(search->hasFocus());
        QTest::keyClicks(search,"pan");QCOMPARE(list->count(),2);QCOMPARE(list->verticalScrollBar()->value(),0);
        const QString selected=panel.currentId();QTest::keyClick(search,Qt::Key_P,Qt::ControlModifier);QVERIFY(history.items("pan").first().pinned);QCOMPARE(panel.currentId(),selected);
        QTest::keyClick(search,Qt::Key_Home);QCOMPARE(search->cursorPosition(),0);QCOMPARE(panel.currentId(),selected);
        QTest::keyClick(search,Qt::Key_Delete);QCOMPARE(panel.filter(),QString("an"));QCOMPARE(history.items().size(),7);
        QTest::keyClick(search,Qt::Key_End);QCOMPARE(search->cursorPosition(),2);QTest::keyClick(search,Qt::Key_Backspace);QCOMPARE(panel.filter(),QString("a"));
        QTest::keyClick(search,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(search,"no-match");QCOMPARE(list->count(),0);
        QSignalSpy choose(&panel,&Panel::chosen);QTest::keyClick(search,Qt::Key_Return);QCOMPARE(choose.size(),0);
        panel.prepareOpen();QVERIFY(panel.filter().isEmpty());QVERIFY(search->hasFocus());QCOMPARE(panel.currentId(),QString("g"));
    }
    void scrollbarDragAcrossGutterAndReleaseInResizeBorder() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();panel.resize(560,300);QApplication::processEvents();QApplication::processEvents();
        auto *search=panel.findChild<QLineEdit*>("filter");auto *list=panel.findChild<QListWidget*>("history");auto *bar=list->verticalScrollBar();
        QVERIFY(bar->isVisible());QCOMPARE(bar->width(),14);QVERIFY(bar->maximum()>0);
        for(int x:{0,6,13}) {
            bar->setValue(0);QStyleOptionSlider option;option.initFrom(bar);option.orientation=Qt::Vertical;option.minimum=bar->minimum();option.maximum=bar->maximum();option.pageStep=bar->pageStep();option.sliderPosition=bar->sliderPosition();option.sliderValue=bar->value();
            const QRect handle=bar->style()->subControlRect(QStyle::CC_ScrollBar,&option,QStyle::SC_ScrollBarSlider,bar);
            const QPoint press(x,handle.center().y());QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,press);QVERIFY(bar->isSliderDown());QVERIFY(search->hasFocus());
            const QPoint outside=bar->mapFrom(&panel,QPoint(panel.width()-2,bar->mapTo(&panel,press).y()+40));
            QMouseEvent move(QEvent::MouseMove,outside,bar->mapToGlobal(outside),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(bar,&move);QVERIFY(bar->value()>0);
            QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,outside);QVERIFY(!bar->isSliderDown());QVERIFY(search->hasFocus());
        }
    }
    void emptyHistoryKeepsSearch() {
        QTemporaryDir dir;History history(dir.path());Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();
        auto *search=panel.findChild<QLineEdit*>("filter");QVERIFY(search->isVisible());QVERIFY(search->isEnabled());QVERIFY(search->hasFocus());QVERIFY(panel.findChild<QToolButton*>("settings")->isVisible());
        QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QVERIFY(!panel.findChild<QPushButton*>("clearAll")->isEnabled());QTest::keyClick(&panel,Qt::Key_Escape);
        QTest::keyClicks(search,"absent");QCOMPARE(panel.filter(),QString("absent"));QTest::keyClick(search,Qt::Key_Escape);QVERIFY(panel.filter().isEmpty());QVERIFY(search->hasFocus());
    }
    void paletteChangesKeepListBackgroundInSync() {
        const QPalette original=qApp->palette();auto restore=qScopeGuard([&]{qApp->setPalette(original);});
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();
        auto *list=panel.findChild<QListWidget*>("history");
        for(const auto color:{QColor("#202326"),QColor("#eff0f1")}) {
            QPalette theme=original;theme.setColor(QPalette::Window,color);qApp->setPalette(theme);QApplication::processEvents();
            QCOMPARE(panel.palette().color(QPalette::Window),color);QCOMPARE(list->palette().color(QPalette::Base),color);QCOMPARE(list->viewport()->palette().color(QPalette::Base),color);
        }
    }
    void cardActionsRespectTopInsetAndKeepSearchFocus() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();
        auto *list=panel.findChild<QListWidget*>("history");auto *search=panel.findChild<QLineEdit*>("filter");QSignalSpy choose(&panel,&Panel::chosen);
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(20,3));QCOMPARE(choose.size(),0);
        const QRect first=list->visualItemRect(list->item(0)).adjusted(0,8,-1,-6);
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,first.center());QCOMPARE(choose.size(),1);QCOMPARE(choose[0][0].toString(),QString("g"));QVERIFY(search->hasFocus());
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(first.right()-17,first.top()+18));QVERIFY(history.items().first().pinned);QVERIFY(search->hasFocus());
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(first.right()-47,first.top()+18));QCOMPARE(history.items().size(),6);QVERIFY(search->hasFocus());
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
        restored.resize(100,100);QApplication::processEvents();QCOMPARE(restored.size(),restored.minimumSize());
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
        QTest::newRow("authorized")<<0;QTest::newRow("denied")<<1;QTest::newRow("invalid-subject")<<2;QTest::newRow("dismissed")<<3;QTest::newRow("agent-error")<<126;
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
        QSignalSpy dismiss(&panel,&Panel::dismissRequested);QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);auto *button=panel.findChild<QPushButton*>("clearAll");
        QTest::mouseClick(button,Qt::LeftButton);QVERIFY(!button->isEnabled());
        QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&panel,&deactivate);QCOMPARE(dismiss.size(),0);QCOMPARE(history.items().size(),7);
        QTRY_VERIFY(panel.findChild<QToolButton*>("back")->isEnabled());
        QCOMPARE(button->isEnabled(),result!=0);
        QCOMPARE(panel.findChild<QLabel*>("clearStatus")->text(),result==0?QString("Borrado; los fijados se conservan."):result==3?QString("No se borró."):QString("No se pudo autorizar."));
        QCOMPARE(history.items().size(),result==0?1:7);QVERIFY(panel.isVisible());QCOMPARE(dismiss.size(),0);
        if(result==0)QVERIFY(history.items().first().pinned);
    }
    void escapeCancelsAuthorization() {
        QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));const auto program=dir.filePath("pkcheck-fake");
        QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nexec sleep 10\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        Panel panel(&history,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();QSignalSpy dismiss(&panel,&Panel::dismissRequested);
        QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("limit50"),Qt::LeftButton);auto *button=panel.findChild<QPushButton*>("clearAll");QTest::mouseClick(button,Qt::LeftButton);QVERIFY(!button->isEnabled());QTest::keyClick(&panel,Qt::Key_Escape);QVERIFY(button->isEnabled());QCOMPARE(history.items().size(),7);QCOMPARE(dismiss.size(),0);QVERIFY(panel.findChild<QLabel*>("saveStatus")->text().isEmpty());
        QTRY_VERIFY(qApp->findChildren<QProcess*>().isEmpty());
    }
    void settingsNavigationAndLiveCounts() {
        QTemporaryDir dir;History h(dir.path());QVERIFY(h.merge(examples()));Panel panel(&h,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();panel.resize(360,240);QApplication::processEvents();const QSize opening=panel.size();
        auto *search=panel.findChild<QLineEdit*>("filter");auto *back=panel.findChild<QToolButton*>("back");auto *save=panel.findChild<QPushButton*>("saveLimit");auto *clear=panel.findChild<QPushButton*>("clearAll");
        QTest::keyClick(search,Qt::Key_Comma,Qt::ControlModifier);QVERIFY(panel.findChild<QWidget*>("settingsPage")->isVisible());QVERIFY(search->isHidden());QVERIFY(panel.findChild<QPushButton*>("limit100")->hasFocus());QVERIFY(!save->isEnabled());QVERIFY(panel.height()>240);
        QCOMPARE(QSettings(dir.filePath("panelrc"),QSettings::IniFormat).value("Window/size").toSize(),opening);
        auto *limit=panel.findChild<QPushButton*>("limit100");QTest::keyClick(limit,Qt::Key_Right);QVERIFY(panel.findChild<QPushButton*>("limit200")->isChecked());QVERIFY(save->isEnabled());
        QTest::keyClick(panel.focusWidget(),Qt::Key_Tab);QVERIFY(save->hasFocus());QTest::keyClick(save,Qt::Key_Tab);QVERIFY(clear->hasFocus());QTest::keyClick(clear,Qt::Key_Tab);QVERIFY(back->hasFocus());QTest::keyClick(back,Qt::Key_Tab);QVERIFY(panel.findChild<QPushButton*>("limit200")->hasFocus());
        QTest::keyClick(&panel,Qt::Key_Delete);QTest::keyClick(&panel,Qt::Key_P,Qt::ControlModifier);QCOMPARE(h.items().size(),7);
        Item item;item.id="extra";item.added=20;item.text="extra";item.payloads["text/plain"]="extra";QVERIFY(h.merge({item}));panel.refresh();QVERIFY(panel.findChild<QLabel*>("historyUsage")->text().startsWith("Ahora: 7 tarjetas y 1 fijada · "));
        QTest::keyClick(&panel,Qt::Key_Escape);QCOMPARE(panel.size(),opening);QVERIFY(search->hasFocus());QCOMPARE(h.maxUnpinned(),100);
        QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QVERIFY(panel.findChild<QPushButton*>("limit100")->isChecked());QTest::mouseClick(panel.findChild<QPushButton*>("limit50"),Qt::LeftButton);panel.prepareOpen();QVERIFY(search->isVisible());QVERIFY(search->hasFocus());
    }
    void settingsSwitchKeepsPlacedWindowInsideWorkArea() {
        QTemporaryDir dir;History h(dir.path());Panel panel(&h,dir.filePath("panelrc"));
        const QRect area(0,0,800,600);panel.prepareOpen(area);panel.show();QApplication::processEvents();
        const QSize opening=panel.size();QVERIFY(opening.height()>=300);
        panel.move(100,area.bottom()-opening.height()+1);const QRect placed=panel.geometry();
        QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QApplication::processEvents();
        QCOMPARE(panel.geometry(),placed);QVERIFY(area.contains(panel.geometry()));
        QVERIFY(h.merge(examples()));panel.refresh();QApplication::processEvents();QCOMPARE(panel.geometry(),placed);
        QTest::keyClick(&panel,Qt::Key_Escape);QApplication::processEvents();QCOMPARE(panel.geometry(),placed);
        QVERIFY(!QSettings(dir.filePath("panelrc"),QSettings::IniFormat).contains("Window/size"));
    }
    void settingsResizePersistsOnlyUserSize() {
        QTemporaryDir dir;History h(dir.path());const auto config=dir.filePath("panelrc");QSize chosen;
        {
            Panel panel(&h,config);panel.prepareOpen(QRect(0,0,1600,1000));panel.show();QApplication::processEvents();
            QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);
            panel.resize(360,240);QApplication::processEvents();chosen=panel.size();
            QCOMPARE(QSettings(config,QSettings::IniFormat).value("Window/size").toSize(),chosen);
            QTest::keyClick(&panel,Qt::Key_Escape);QApplication::processEvents();QCOMPARE(panel.size(),chosen);
        }
        Panel restored(&h,config);restored.prepareOpen(QRect(0,0,1600,1000));restored.show();QApplication::processEvents();QCOMPARE(restored.size(),chosen);
    }
    void shortWorkAreaScrollsSettingsWithoutCompressingLabels() {
        QTemporaryDir dir;History h(dir.path());QVector<Item> entries=examples();
        for(int i=0;i<80;++i){Item item;item.id=QString::number(i);item.added=i+20;item.payloads["text/plain"]="x";entries<<item;}QVERIFY(h.merge(entries));
        Panel panel(&h,dir.filePath("panelrc"));panel.prepareOpen(QRect(0,0,360,240));panel.show();QApplication::processEvents();
        QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("limit50"),Qt::LeftButton);QApplication::processEvents();
        QCOMPARE(panel.size(),QSize(360,240));auto *scroll=panel.findChild<QScrollArea*>();QVERIFY(scroll);QVERIFY(scroll->verticalScrollBar()->maximum()>0);
        QCOMPARE(scroll->frameShape(),QFrame::NoFrame);QCOMPARE(scroll->horizontalScrollBarPolicy(),Qt::ScrollBarAlwaysOff);
        for(const auto name:{"historyUsage","retentionWarning","saveStatus","clearStatus"}) {
            auto *label=panel.findChild<QLabel*>(name);QVERIFY(label->isVisible());QVERIFY(label->height()>=label->heightForWidth(label->width()));
        }
        auto *clear=panel.findChild<QPushButton*>("clearAll");scroll->ensureWidgetVisible(clear);QApplication::processEvents();
        QVERIFY(scroll->verticalScrollBar()->value()>0);QVERIFY(scroll->viewport()->rect().contains(QRect(clear->mapTo(scroll->viewport(),QPoint()),clear->size())));
        auto *bar=scroll->verticalScrollBar();bar->setValue(0);QStyleOptionSlider option;option.initFrom(bar);option.orientation=Qt::Vertical;option.minimum=0;option.maximum=bar->maximum();option.pageStep=bar->pageStep();option.sliderPosition=0;option.sliderValue=0;
        const auto handle=bar->style()->subControlRect(QStyle::CC_ScrollBar,&option,QStyle::SC_ScrollBarSlider,bar);
        const QPoint press(bar->width()-1,handle.center().y());QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,press);QVERIFY(bar->isSliderDown());QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,press);QVERIFY(!bar->isSliderDown());
        QTest::keyClick(&panel,Qt::Key_Escape);QApplication::processEvents();QCOMPARE(panel.size(),QSize(360,240));
    }
    void settingsEnterAutoRepeatDoesNotAuthorize_data() {
        QTest::addColumn<int>("key");QTest::addColumn<bool>("saving");
        for(int key:{Qt::Key_Return,Qt::Key_Enter})for(bool saving:{false,true})QTest::newRow(qPrintable(QString::number(key)+(saving?"-save":"-clear")))<<key<<saving;
    }
    void settingsEnterAutoRepeatDoesNotAuthorize() {
        QFETCH(int,key);QFETCH(bool,saving);QTemporaryDir dir;History h(dir.path());QVERIFY(h.merge(examples()));
        const auto program=dir.filePath("pkcheck-fake"),calls=dir.filePath("calls");QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\necho called >> '"+calls.toUtf8()+"'\nexit 1\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        Panel panel(&h,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);
        auto *target=panel.findChild<QPushButton*>(saving?"limit200":"clearAll");if(saving)target->click();target->setFocus();QTest::keyClick(target,Qt::Key(key));
        QTRY_VERIFY(panel.findChild<QToolButton*>("back")->isEnabled());
        // A failed save resets the preset; selecting it again makes a repeated Enter actionable.
        if(saving)target->click();target->setFocus();
        for(int i=0;i<3;++i) {
            QKeyEvent repeat(QEvent::KeyPress,key,Qt::NoModifier,QString(),true,1);QApplication::sendEvent(target,&repeat);
            QTRY_VERIFY(panel.findChild<QToolButton*>("back")->isEnabled());
            if(saving)target->click();target->setFocus();
        }
        QFile log(calls);QVERIFY(log.open(QIODevice::ReadOnly));QCOMPARE(log.readAll(),QByteArray("called\n"));QCOMPARE(h.items().size(),7);
    }
    void saveAuthorization_data() {clearAuthorization_data();}
    void saveAuthorization() {
        QFETCH(int,result);QTemporaryDir dir;History h(dir.path());QVector<Item> entries;
        for(int i=0;i<87;++i){Item item;item.id=QString::number(i);item.added=i;item.text=item.id;item.payloads["text/plain"]=item.text.toUtf8();entries<<item;}
        Item pin;pin.id="pin";pin.added=-1;pin.pinned=true;pin.payloads["text/plain"]="pinned";entries<<pin;QVERIFY(h.merge(entries));
        const auto program=dir.filePath("pkcheck-fake");if(result!=-1) {
            QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nprintf '%s\n' \"$@\" > '"+dir.filePath("arguments").toUtf8()+"'\nsleep 0.1\n");script.write(result==-2?QByteArray("kill -KILL $$\n"):QByteArray("exit ")+QByteArray::number(result)+"\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        }
        Panel panel(&h,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);
        auto *limit=panel.findChild<QPushButton*>("limit50");QTest::mouseClick(limit,Qt::LeftButton);QCOMPARE(h.maxUnpinned(),100);QCOMPARE(panel.findChild<QLabel*>("retentionWarning")->text(),QString("Se borrarán las 37 tarjetas más antiguas; los fijados se conservan."));
        QSignalSpy dismiss(&panel,&Panel::dismissRequested);QTest::keyClick(limit,Qt::Key_Return);QVERIFY(!limit->isEnabled());QVERIFY(!panel.findChild<QToolButton*>("back")->isEnabled());QCOMPARE(h.usage().unpinned,87);
        QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&panel,&deactivate);QCOMPARE(dismiss.size(),0);
        QTRY_VERIFY(limit->isEnabled());QCOMPARE(h.maxUnpinned(),result==0?50:100);QCOMPARE(h.usage().unpinned,result==0?50:87);QCOMPARE(h.usage().pinned,1);
        QCOMPARE(panel.findChild<QLabel*>("saveStatus")->text(),result==0?QString("Guardado; se borraron 37."):result==3?QString("No se guardó; sigue en 100."):QString("No se pudo autorizar; sigue en 100."));
        QVERIFY(panel.findChild<QPushButton*>(result==0?"limit50":"limit100")->hasFocus());QVERIFY(!panel.findChild<QPushButton*>("saveLimit")->isEnabled());
        if(result!=-1){QFile arguments(dir.filePath("arguments"));QVERIFY(arguments.open(QIODevice::ReadOnly));QVERIFY(arguments.readAll().contains("org.local.clipboard-panel.change-limit"));}
    }
    void saveCancellationAndHide() {
        for(bool hide:{false,true}) {
            QTemporaryDir dir;History h(dir.path());QVERIFY(h.merge(examples()));const auto program=dir.filePath("pkcheck-fake");QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nexec sleep 10\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            Panel panel(&h,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("limit200"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("saveLimit"),Qt::LeftButton);
            if(hide)panel.hide();else QTest::keyClick(&panel,Qt::Key_Escape);
            QCOMPARE(h.maxUnpinned(),100);QCOMPARE(h.items().size(),7);QCOMPARE(panel.findChild<QLabel*>("saveStatus")->text(),QString("No se guardó; sigue en 100."));QVERIFY(panel.findChild<QPushButton*>("limit100")->isChecked());QTRY_VERIFY(qApp->findChildren<QProcess*>().isEmpty());
            panel.prepareOpen();panel.show();QVERIFY(panel.findChild<QLineEdit*>("filter")->isVisible());
        }
    }
    void authorizationTimeout() {
        QTemporaryDir dir;History h(dir.path());QVERIFY(h.merge(examples()));const auto program=dir.filePath("pkcheck-fake");QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nexec sleep 10\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        Panel panel(&h,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("limit200"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("saveLimit"),Qt::LeftButton);
        auto *timeout=panel.findChild<QProcess*>()->findChild<QTimer*>();QVERIFY(timeout);QCOMPARE(timeout->interval(),120000);timeout->start(1);QTRY_VERIFY(panel.findChild<QToolButton*>("back")->isEnabled());QCOMPARE(h.maxUnpinned(),100);QCOMPARE(panel.findChild<QLabel*>("saveStatus")->text(),QString("No se pudo autorizar; sigue en 100."));QTRY_VERIFY(qApp->findChildren<QProcess*>().isEmpty());
    }
    void singularCopyAndEmptySettings() {
        QTemporaryDir dir;History h(dir.path());QVector<Item> entries;
        for(int i=0;i<51;++i){Item item;item.id=QString::number(i);item.added=i;item.payloads["text/plain"]="x";entries<<item;}QVERIFY(h.merge(entries));
        Panel panel(&h,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("limit50"),Qt::LeftButton);QCOMPARE(panel.findChild<QLabel*>("retentionWarning")->text(),QString("Se borrará la tarjeta más antigua."));QVERIFY(h.clear());panel.refresh();QCOMPARE(panel.findChild<QLabel*>("historyUsage")->text(),QString("Ahora no hay tarjetas."));QVERIFY(!panel.findChild<QPushButton*>("clearAll")->isEnabled());QCOMPARE(panel.findChild<QLabel*>("clearStatus")->text(),QString("No hay tarjetas para borrar."));
        entries.resize(1);entries[0].id="new";QVERIFY(h.merge(entries));panel.refresh();QCOMPARE(panel.findChild<QLabel*>("clearStatus")->text(),QString("Solo en este panel; conserva los fijados."));QVERIFY(h.pin("new"));panel.refresh();QVERIFY(panel.findChild<QLabel*>("historyUsage")->text().startsWith("Ahora: 0 tarjetas y 1 fijada · "));
    }
    void databaseFailureDoesNotSaveOrTrim() {
        QTemporaryDir dir;History h(dir.path());QVector<Item> entries;
        for(int i=0;i<87;++i){Item item;item.id=QString::number(i);item.added=i;item.payloads["text/plain"]="synthetic";entries<<item;}QVERIFY(h.merge(entries));
        const auto program=dir.filePath("pkcheck-fake");QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nsleep 0.1\nexit 0\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        Panel panel(&h,dir.filePath("panelrc"),program);panel.prepareOpen();panel.show();QApplication::processEvents();QTest::mouseClick(panel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("limit50"),Qt::LeftButton);
        const QString connection="ui-commit-lock";
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(dir.filePath("history.sqlite"));QVERIFY(db.open());QVERIFY(db.transaction());QSqlQuery lock(db);QVERIFY(lock.exec("SELECT * FROM items"));QVERIFY(lock.next());
            QTest::mouseClick(panel.findChild<QPushButton*>("saveLimit"),Qt::LeftButton);QTRY_VERIFY(panel.findChild<QToolButton*>("back")->isEnabled());QCOMPARE(h.maxUnpinned(),100);QCOMPARE(h.usage().unpinned,87);QCOMPARE(panel.findChild<QLabel*>("saveStatus")->text(),QString("No se guardó; sigue en 100."));lock.finish();QVERIFY(db.rollback());
        }
        QSqlDatabase::removeDatabase(connection);
        QTest::mouseClick(panel.findChild<QPushButton*>("limit50"),Qt::LeftButton);QTest::mouseClick(panel.findChild<QPushButton*>("saveLimit"),Qt::LeftButton);QTRY_COMPARE(h.maxUnpinned(),50);QCOMPARE(h.usage().unpinned,50);
    }
    void firstOpenPerformance_data() {QTest::addColumn<int>("cards");QTest::newRow("1000")<<1000;QTest::newRow("500")<<500;}
    void firstOpenPerformance() {
        if(!qEnvironmentVariableIsSet("CLIPBOARD_PANEL_PERF"))QSKIP("Opt-in synthetic first-open measurement");
        QFETCH(int,cards);QTemporaryDir dir;
        {History seed(dir.path());QVERIFY(seed.isOpen());}
        const QString connection="performance-fixture";
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(dir.filePath("history.sqlite"));QVERIFY(db.open());QVERIFY(db.transaction());
            const auto picture=examples()[1].payloads["image/png"];
            QSqlQuery item(db),payload(db);item.prepare("INSERT INTO items(id,sequence,added,text,pinned) VALUES(?,?,?,?,0)");payload.prepare("INSERT INTO payloads VALUES(?,?,?)");
            for(int i=0;i<cards;++i){const auto id=QString::number(i);item.bindValue(0,id);item.bindValue(1,i+1);item.bindValue(2,i);item.bindValue(3,i%5?"Synthetic clipboard text":"");QVERIFY(item.exec());payload.bindValue(0,id);payload.bindValue(1,i%5?"text/plain":"image/png");payload.bindValue(2,i%5?QByteArray("Synthetic clipboard text"):picture);QVERIFY(payload.exec());}
            QVERIFY(db.commit());
        }
        QSqlDatabase::removeDatabase(connection);
        QElapsedTimer timer;timer.start();History history(dir.path());Panel panel(&history,dir.filePath("panelrc"));const auto constructed=timer.elapsed();panel.prepareOpen();panel.show();QApplication::processEvents();qInfo("FIRST_OPEN %d cards / %d PNG 1920x1080: constructor=%lld ms; visible=%lld ms",cards,cards/5,constructed,timer.elapsed());QCOMPARE(history.items().size(),cards);
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
        palette.setColor(QPalette::PlaceholderText,palette.color(QPalette::Disabled,QPalette::Text));qApp->setPalette(palette);
        QVERIFY(QDir().mkpath(output));QTemporaryDir dir;History history(dir.path());QVERIFY(history.merge(examples()));QVERIFY(history.choose("f"));Panel panel(&history,dir.filePath("panelrc"));panel.prepareOpen();panel.show();QApplication::processEvents();
        auto render=[&](const QString &name){QApplication::processEvents();QImage image(panel.size()*2,QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(2);image.fill(Qt::transparent);panel.render(&image);QVERIFY(image.save(QDir(output).filePath(name+".png")));};
        panel.prepareOpen(QRect(0,0,1600,1000));panel.resize(420,400);render("resize-420x400");
        panel.resize(800,700);render("resize-800x700");
        render("normal-selected");auto *list=panel.findChild<QListWidget*>("history");list->clearSelection();render("normal-hoverless");list->setCurrentRow(1);render("selected-image");
        auto *search=panel.findChild<QLineEdit*>("filter");QTest::keyClicks(search,"pan");render("filter");search->setText("no-existe");render("filter-empty");search->clear();panel.prepareOpen();
        panel.resize(560,700);
        const QPalette desktopPalette=qApp->palette();
        for(bool dark:{true,false}) {
            QPalette theme=desktopPalette;
            theme.setColor(QPalette::Window,QColor(dark?"#202326":"#eff0f1"));theme.setColor(QPalette::WindowText,QColor(dark?"#fcfcfc":"#232629"));
            theme.setColor(QPalette::Base,QColor(dark?"#141618":"#ffffff"));theme.setColor(QPalette::Text,QColor(dark?"#fcfcfc":"#232629"));
            theme.setColor(QPalette::Button,QColor(dark?"#292c30":"#fcfcfc"));theme.setColor(QPalette::ButtonText,theme.color(QPalette::Text));
            theme.setColor(QPalette::Highlight,QColor("#3daee9"));theme.setColor(QPalette::Link,QColor(dark?"#1d99f3":"#2980b9"));
            theme.setColor(QPalette::Disabled,QPalette::Text,QColor(dark?"#a1a9b1":"#707d8a"));theme.setColor(QPalette::PlaceholderText,theme.color(QPalette::Disabled,QPalette::Text));
            qApp->setPalette(theme);QApplication::processEvents();panel.prepareOpen();render(dark?"dark-default":"light-default");
            QTest::keyClicks(search,"pan");render(dark?"dark-searching":"light-searching");search->setText("no-existe");render(dark?"dark-no-results":"light-no-results");
            QTemporaryDir fixture;History settingsHistory(fixture.path());auto entries=examples();
            for(int i=0;i<81;++i){Item item;item.id=QStringLiteral("extra%1").arg(i);item.added=i+20;item.text=QStringLiteral("Tarjeta de prueba %1").arg(i);item.payloads["text/plain"]=item.text.toUtf8();entries<<item;}
            for(int i=0;i<2;++i){Item item;item.id=QStringLiteral("pin%1").arg(i);item.pinned=true;item.added=-i;item.payloads["text/plain"]="Fijada";entries<<item;}
            QVERIFY(settingsHistory.merge(entries));
            const auto program=fixture.filePath("pkcheck-fake"),resultFile=fixture.filePath("result");QFile script(program);QVERIFY(script.open(QIODevice::WriteOnly));script.write("#!/bin/sh\nsleep 0.1\nexit \"$(cat '"+resultFile.toUtf8()+"')\"\n");script.close();QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            Panel settingsPanel(&settingsHistory,fixture.filePath("panelrc"),program);settingsPanel.prepareOpen(QRect(0,0,1600,1000));settingsPanel.show();QApplication::processEvents();settingsPanel.resize(560,360);
            auto settingsRender=[&](const QString &name){QApplication::processEvents();QImage image(settingsPanel.size()*2,QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(2);image.fill(Qt::transparent);settingsPanel.render(&image);QVERIFY(image.save(QDir(output).filePath((dark?"dark-":"light-")+name+".png")));};
            settingsRender("lista");QTest::mouseClick(settingsPanel.findChild<QToolButton*>("settings"),Qt::LeftButton);settingsRender("ajustes");
            QTest::mouseClick(settingsPanel.findChild<QPushButton*>("limit50"),Qt::LeftButton);settingsRender("bajar");settingsPanel.resize(360,346);settingsRender("bordes");
            settingsPanel.prepareOpen(QRect(0,0,360,240));QTest::mouseClick(settingsPanel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(settingsPanel.findChild<QPushButton*>("limit50"),Qt::LeftButton);settingsRender("ajustes-360x240");
            auto *scroll=settingsPanel.findChild<QScrollArea*>();scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());settingsRender("ajustes-360x240-scroll");
            settingsPanel.prepareOpen(QRect(0,0,1600,1000));QTest::mouseClick(settingsPanel.findChild<QToolButton*>("settings"),Qt::LeftButton);QTest::mouseClick(settingsPanel.findChild<QPushButton*>("limit50"),Qt::LeftButton);settingsPanel.resize(560,360);
            QFile result(resultFile);QVERIFY(result.open(QIODevice::WriteOnly));result.write("3");result.close();QTest::mouseClick(settingsPanel.findChild<QPushButton*>("saveLimit"),Qt::LeftButton);settingsRender("esperando");QTRY_VERIFY(settingsPanel.findChild<QToolButton*>("back")->isEnabled());settingsRender("cancelado");
            QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::Truncate));result.write("0");result.close();QTest::mouseClick(settingsPanel.findChild<QPushButton*>("limit50"),Qt::LeftButton);QTest::mouseClick(settingsPanel.findChild<QPushButton*>("saveLimit"),Qt::LeftButton);QTRY_COMPARE(settingsHistory.maxUnpinned(),50);settingsRender("guardado");
            QTest::mouseClick(settingsPanel.findChild<QPushButton*>("clearAll"),Qt::LeftButton);QTRY_COMPARE(settingsHistory.usage().unpinned,0);settingsRender("borrado");

        }
        qApp->setPalette(desktopPalette);panel.prepareOpen();
        for(const auto &item:history.items())history.remove(item.id);panel.refresh(true);render("empty");
    }
};
int main(int argc,char **argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);
    if(QStyleFactory::keys().contains("Breeze",Qt::CaseInsensitive))QApplication::setStyle("Breeze");
    PanelTest test;return QTest::qExec(&test,argc,argv);
}
#include "panel-test.moc"
