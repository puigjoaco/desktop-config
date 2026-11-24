#include "panel.h"
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>
#include <unistd.h>

namespace Clip {
static QColor mix(QColor a, QColor b, qreal amount) {
    return QColor::fromRgbF(a.redF()*(1-amount)+b.redF()*amount,a.greenF()*(1-amount)+b.greenF()*amount,a.blueF()*(1-amount)+b.blueF()*amount);
}
static void icon(QPainter &p, QPointF at, const QString &kind, QColor color) {
    p.save(); p.translate(at); p.setPen(QPen(color,1.3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    if (kind==QStringLiteral("delete")) {
        p.drawLine(QPointF(3,4),QPointF(13,4)); p.drawRect(QRectF(6,2,4,2)); p.drawPolyline(QPolygonF{{4,5},{5,14},{11,14},{12,5}}); p.drawLine(QPointF(7,7),QPointF(7,11)); p.drawLine(QPointF(9,7),QPointF(9,11));
    } else if (kind==QStringLiteral("pin")) {
        p.drawPolygon(QPolygonF{{10,2},{14,6},{12,7},{10,9},{10,12},{4,6},{7,6},{9,4}}); p.drawLine(QPointF(6,10),QPointF(2,14));
    } else {
        p.drawLine(QPointF(6,10),QPointF(10,6)); p.drawArc(QRectF(7,1,7,7),-45*16,270*16); p.drawArc(QRectF(2,8,7,7),135*16,270*16);
    }
    p.restore();
}
static QRect actionRect(const QRect &card, bool pin) { return QRect(card.right()-(pin?31:61),card.top()+4,28,28); }
static int cardHeight(const Item &item, QSize imageSize, int width, const QFont &font, bool lastChosen) {
    if (!imageSize.isEmpty()) return imageSize.scaled(QSize(qMax(1,width-112),200),Qt::KeepAspectRatio).height()+20;
    if (isLink(cleanText(item.text))) return 40;
    QTextLayout text(cleanText(item.text).replace(QChar('\n'),QChar::LineSeparator),font); QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); text.setTextOption(option);
    text.beginLayout(); int lines=0;
    while (lines<3) { auto line=text.createLine(); if(!line.isValid()) break; line.setLineWidth(qMax(20,width-24-(lastChosen?(item.pinned?116:96):(item.pinned?24:0)))); ++lines; }
    text.endLayout(); return qMax(40,lines*QFontMetrics(font).lineSpacing()+20);
}
class EmptyLabel final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const QColor muted=palette().color(QPalette::WindowText);
        const int top=(height()-82)/2;
        p.save();p.translate((width()-48)/2,top);p.setOpacity(.55);p.setPen(QPen(muted,2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        p.drawRoundedRect(QRectF(11,9,26,33),3,3);p.setBrush(palette().color(QPalette::Window));p.drawRoundedRect(QRectF(18,5.5,12,7),2,2);
        p.drawLine(QPointF(17,21),QPointF(31,21));p.drawLine(QPointF(17,27),QPointF(31,27));p.drawLine(QPointF(17,33),QPointF(26,33));p.restore();
        QFont f=font();f.setPointSize(12);p.setFont(f);p.setPen(muted);p.drawText(QRect(0,top+60,width(),24),Qt::AlignCenter,text());
    }
};
class CardDelegate final : public QStyledItemDelegate {
public:
    QVector<Item> entries;
    QString last;
    QString filter;
    QPalette theme;
    QMap<QString,ImagePreview> previews;
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p,const QStyleOptionViewItem &o,const QModelIndex &index) const override {
        if(index.row()>=entries.size()) return;
        const auto &item=entries[index.row()];
        const QRect card=o.rect.adjusted(0,0,-1,index.row()==entries.size()-1?0:-6);
        const auto &palette=theme;
        const QColor fg=palette.color(QPalette::Text), highlight=palette.color(QPalette::Highlight), muted=palette.color(QPalette::Disabled,QPalette::Text);
        const bool selected=o.state & QStyle::State_Selected, hover=o.state & QStyle::State_MouseOver;
        QColor bg=palette.color(QPalette::Button);
        if(selected||hover) bg=mix(bg,highlight,selected?.25:.12);
        p->save(); p->setRenderHint(QPainter::Antialiasing); p->setBrush(bg);
        p->setPen(QPen(selected?highlight:hover?mix(bg,highlight,.45):mix(bg,fg,.10),selected?2:1));
        p->drawRoundedRect(QRectF(card).adjusted(1,1,-1,-1),5,5);
        QRect content=card.adjusted(12,10,-12,-10);
        const bool lastChosen=item.id==last;
        const int reserve=lastChosen?(item.pinned?116:96):(item.pinned?24:0);
        const auto preview=previews.value(item.id);
        const QImage &image=preview.thumbnail;
        p->setFont(o.font); p->setPen(fg);
        if(!image.isNull()) {
            const QSize size=preview.original.scaled(QSize(qMax(1,content.width()-88),200),Qt::KeepAspectRatio);
            const QRect thumb(content.topLeft(),size);
            QPainterPath clip; clip.addRoundedRect(thumb,3,3); p->save(); p->setClipPath(clip); p->drawImage(thumb,image); p->restore();
            QFont small=o.font; small.setPointSize(8); p->setFont(small);
            const QString dimensions=QStringLiteral("%1 × %2").arg(preview.original.width()).arg(preview.original.height());
            const int chipWidth=QFontMetrics(small).horizontalAdvance(dimensions)+10;
            QRect chip(thumb.right()-chipWidth-3,thumb.bottom()-20,chipWidth,17);
            // Palette-based contrast, including the chip, follows light and dark themes.
            p->setPen(Qt::NoPen); p->setBrush(palette.color(QPalette::Base)); p->drawRoundedRect(chip,3,3); p->setPen(fg); p->drawText(chip,Qt::AlignCenter,dimensions);
        } else if(isLink(cleanText(item.text))) {
            icon(*p,content.topLeft(),QStringLiteral("link"),muted); p->setPen(palette.color(QPalette::Link));
            QRect textRect=content.adjusted(24,0,-reserve,0);
            const QString display=QFontMetrics(o.font).elidedText(cleanText(item.text),Qt::ElideRight,textRect.width());
            QTextLayout link(display,o.font);QList<QTextLayout::FormatRange> matches;
            if(!filter.isEmpty())for(int pos=display.indexOf(filter,0,Qt::CaseInsensitive);pos>=0;pos=display.indexOf(filter,pos+filter.size(),Qt::CaseInsensitive)) {
                QTextLayout::FormatRange match;match.start=pos;match.length=filter.size();match.format.setBackground(mix(bg,highlight,.38));matches.append(match);
            }
            link.setFormats(matches);link.beginLayout();auto line=link.createLine();line.setLineWidth(textRect.width());link.endLayout();line.draw(p,QPointF(textRect.x(),textRect.y()+(textRect.height()-line.height())/2));
        } else {
            const QString cleaned=cleanText(item.text).replace(QChar('\n'),QChar::LineSeparator);
            QTextLayout layout(cleaned,o.font); QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
            QList<QTextLayout::FormatRange> formats;
            if(!filter.isEmpty()) for(int pos=cleaned.indexOf(filter,0,Qt::CaseInsensitive);pos>=0;pos=cleaned.indexOf(filter,pos+filter.size(),Qt::CaseInsensitive)) {
                QTextLayout::FormatRange f; f.start=pos;f.length=filter.size();f.format.setBackground(mix(bg,highlight,.38));formats.append(f);
            }
            layout.setFormats(formats); layout.beginLayout();
            int count=0; qreal y=0;
            while(count<3) {
                auto line=layout.createLine(); if(!line.isValid()) break; line.setLineWidth(qMax(20,content.width()-reserve)); line.setPosition(QPointF(0,y)); y+=line.height(); ++count;
            }
            layout.endLayout();
            p->setPen(fg);
            for(int n=0;n<layout.lineCount();++n) {
                auto line=layout.lineAt(n);
                if(n==2 && line.textStart()+line.textLength()<cleaned.size()) {
                    const auto tail=cleaned.mid(line.textStart()).replace(QChar::LineSeparator,QChar(' '));
                    p->drawText(QPointF(content.x(),content.y()+line.y()+line.ascent()),QFontMetrics(o.font).elidedText(tail,Qt::ElideRight,line.width()));
                } else line.draw(p,content.topLeft());
            }
        }
        if(!hover) {
            if(item.pinned) icon(*p,QPointF(card.right()-26,card.top()+10),QStringLiteral("pin"),highlight);
            if(lastChosen) { QFont small=o.font;small.setPointSize(8);p->setFont(small);p->setPen(muted);p->drawText(card.adjusted(12,10,item.pinned?-34:-12,0),Qt::AlignRight|Qt::AlignTop,QStringLiteral("Último elegido")); }
        } else {
            const QRect actions(card.right()-64,card.top()+3,61,32);
            p->setPen(Qt::NoPen);p->setBrush(bg);p->drawRoundedRect(actions,4,4);
            icon(*p,actionRect(card,false).topLeft()+QPoint(6,6),QStringLiteral("delete"),fg);
            icon(*p,actionRect(card,true).topLeft()+QPoint(6,6),QStringLiteral("pin"),item.pinned?highlight:fg);
        }
        p->restore();
    }
};
Panel::Panel(History *history, const QString &settingsFile, const QString &authorizationProgram)
    : QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint),history(history),
      settingsFile(settingsFile.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+QStringLiteral("/clipboard-panelrc"):settingsFile),
      authorizationProgram(authorizationProgram) {
    QSettings settings(this->settingsFile,QSettings::IniFormat);
    preferredSize=settings.value(QStringLiteral("Window/size")).toSize();
    customSize=preferredSize.isValid()&&!preferredSize.isEmpty();
    setObjectName(QStringLiteral("clipboardPanel")); setWindowTitle(QStringLiteral("Portapapeles"));
    setAttribute(Qt::WA_TranslucentBackground); setMinimumSize(360,240); resizeInternally(QSize(560,240)); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
    QFont f=font();f.setPointSize(10);setFont(f);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);layout->setSpacing(4);
    header=new QWidget(this);header->setFixedHeight(36);auto *head=new QHBoxLayout(header);head->setContentsMargins(12,0,0,0);
    auto *title=new QLabel(QStringLiteral("Portapapeles"));QFont bold=f;bold.setWeight(QFont::DemiBold);title->setFont(bold);head->addWidget(title);head->addStretch();
    clearButton=new QPushButton(QStringLiteral("Borrar todo"));clearButton->setFlat(true);clearButton->setFixedHeight(28);clearButton->setToolTip(QStringLiteral("Borrar solo de este panel; conserva los fijados"));head->addWidget(clearButton);layout->addWidget(header);
    search=new QLineEdit(this);search->setObjectName(QStringLiteral("filter"));search->setAccessibleName(QStringLiteral("Filtrar portapapeles"));search->setClearButtonEnabled(true);search->setFixedHeight(36);search->hide();layout->addWidget(search);search->installEventFilter(this);
    list=new QListWidget(this);list->setObjectName(QStringLiteral("history"));list->setFrameShape(QFrame::NoFrame);list->setMouseTracking(true);list->setSelectionMode(QAbstractItemView::SingleSelection);list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);list->setFocusPolicy(Qt::StrongFocus);list->setItemDelegate(new CardDelegate(list));list->viewport()->setAttribute(Qt::WA_Hover);list->viewport()->installEventFilter(this);list->installEventFilter(this);list->setAccessibleName(QStringLiteral("Historial, más reciente primero"));
    QPalette lp=list->palette();lp.setColor(QPalette::Base,palette().color(QPalette::Window));list->setPalette(lp);
    list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; } QScrollBar:vertical { width: 4px; background: transparent; } QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 24px; } QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; } QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }").arg(palette().color(QPalette::Disabled,QPalette::Text).name()));
    layout->addWidget(list,1);
    empty=new EmptyLabel(this);empty->setAlignment(Qt::AlignCenter);empty->setMinimumHeight(152);QPalette ep=empty->palette();ep.setColor(QPalette::WindowText,palette().color(QPalette::Disabled,QPalette::Text));empty->setPalette(ep);layout->addWidget(empty);
    clearButton->setObjectName(QStringLiteral("clearAll"));clearButton->setAutoDefault(false);
    connect(clearButton,&QPushButton::clicked,this,&Panel::requestClear);
    clearButton->installEventFilter(this);
    connect(search,&QLineEdit::textChanged,this,[this]{refresh(true);});
    for(auto *child:findChildren<QWidget*>()) { child->setMouseTracking(true);child->installEventFilter(this); }
    refresh(true);
}
QString Panel::currentId() const { auto *item=list->currentItem();return item?item->data(Qt::UserRole).toString():QString(); }
QString Panel::filter() const { return search->text(); }
void Panel::prepareOpen(const QRect &availableArea) {
    QRect area=availableArea;
    if(!area.isValid()) {
        auto *screen=QGuiApplication::screenAt(QCursor::pos());
        if(!screen)screen=QGuiApplication::primaryScreen();
        if(screen)area=screen->availableGeometry();
    }
    adjustingSize=true;
    if(area.isValid()) { setMinimumSize(QSize(360,240).boundedTo(area.size()));setMaximumSize(area.size()); }
    adjustingSize=false;
    resizeInternally(customSize?preferredSize:QSize(560,height()));
    search->clear();search->hide();header->show();refresh(true);list->setFocus();
}
void Panel::resizeInternally(const QSize &size) {
    adjustingSize=true;
    const QSize target=size.expandedTo(minimumSize()).boundedTo(maximumSize());
    if (this->size()!=target) {
        pendingAutomaticSize=target;
        resize(target);
    }
    adjustingSize=false;
}
void Panel::queueCardSizes() {
    if(cardSizesPending)return;
    cardSizesPending=true;
    QTimer::singleShot(0,this,[this]{cardSizesPending=false;updateCardSizes();});
}
void Panel::updateCardSizes() {
    if(!list)return;
    auto *delegate=static_cast<CardDelegate*>(list->itemDelegate());
    const int width=qMax(1,list->viewport()->width());
    int height=0;
    for(int row=0;row<list->count();++row) {
        const auto &item=delegate->entries[row];
        const int rowHeight=cardHeight(item,delegate->previews.value(item.id).original,width-1,font(),item.id==delegate->last)+(row==list->count()-1?0:6);
        list->item(row)->setSizeHint(QSize(width,rowHeight));height+=rowHeight;
    }
    if(!customSize)resizeInternally(QSize(width==1?560:this->width(),qMin(760,56+(list->count()?qMax(40,height):152))));
    list->doItemsLayout();list->viewport()->update();
}
void Panel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    const bool automatic=adjustingSize||event->size()==pendingAutomaticSize;
    pendingAutomaticSize={}; // Suppress only the pending internal resize, not this size forever.
    if(isVisible()&&!automatic) {
        customSize=true;preferredSize=event->size();
        QSettings settings(settingsFile,QSettings::IniFormat);
        settings.setValue(QStringLiteral("Window/size"),preferredSize);settings.sync();
    }
    queueCardSizes();
}
Qt::Edges Panel::resizeEdges(const QPoint &position) const {
    Qt::Edges edges;
    if(position.x()<6)edges|=Qt::LeftEdge;
    else if(position.x()>=width()-6)edges|=Qt::RightEdge;
    if(position.y()<6)edges|=Qt::TopEdge;
    else if(position.y()>=height()-6)edges|=Qt::BottomEdge;
    return edges;
}
void Panel::requestClear() {
    if(authorizationPending)return;
    authorizationPending=true;clearButton->setEnabled(false);clearButton->setText(QStringLiteral("Autorizando…"));
    authorization=new QProcess(this);
    connect(authorization,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){finishAuthorization(status==QProcess::NormalExit&&code==0);});
    connect(authorization,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finishAuthorization(false);});
    // Include start time and uid to bind polkit's subject to this exact process.
    QFile stat(QStringLiteral("/proc/self/stat"));
    if(!stat.open(QIODevice::ReadOnly)){finishAuthorization(false);return;}
    const QByteArray fields=stat.readAll();const auto values=fields.mid(fields.lastIndexOf(')')+2).split(' ');
    if(values.size()<=19){finishAuthorization(false);return;}
    const QString subject=QStringLiteral("%1,%2,%3").arg(QCoreApplication::applicationPid()).arg(QString::fromLatin1(values[19])).arg(::getuid());
    authorization->start(authorizationProgram,{QStringLiteral("--action-id"),QStringLiteral("org.local.clipboard-panel.clear-history"),QStringLiteral("--process"),subject,QStringLiteral("--allow-user-interaction")});
    auto *timeout=new QTimer(authorization);timeout->setSingleShot(true);
    connect(timeout,&QTimer::timeout,this,[this]{if(authorizationPending)finishAuthorization(false);});timeout->start(120000);
}
void Panel::finishAuthorization(bool authorized) {
    if(!authorizationPending)return;
    authorizationPending=false;
    auto *process=authorization;authorization=nullptr;
    process->disconnect(this);process->setParent(qApp);
    if(process->state()!=QProcess::NotRunning)process->kill();
    // Reap asynchronously; never wait for a child in the GUI thread.
    if(process->state()==QProcess::NotRunning)process->deleteLater();
    else connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
    const bool cleared=authorized&&history->clear();
    clearButton->setEnabled(true);clearButton->setText(cleared?QStringLiteral("Borrar todo"):QStringLiteral("No se borró"));
    if(cleared)refresh(true);
    else QTimer::singleShot(2500,this,[this]{if(!authorizationPending)clearButton->setText(QStringLiteral("Borrar todo"));});
}
void Panel::refresh(bool selectNewest) {
    const QString selected=selectNewest?QString():currentId(); const int oldRow=list->currentRow(); const int scroll=list->verticalScrollBar()->value();
    auto *delegate=static_cast<CardDelegate*>(list->itemDelegate());delegate->entries=history->items(search->text());delegate->last=history->lastChosen();delegate->filter=search->text();delegate->theme=palette();
    QSet<QString> visibleIds;
    for(const auto &entry:delegate->entries) {
        visibleIds.insert(entry.id);
        if(!delegate->previews.contains(entry.id)) {
            delegate->previews.insert(entry.id,history->imagePreview(entry.id));
        }
    }
    for(auto it=delegate->previews.begin();it!=delegate->previews.end();) {
        if(!visibleIds.contains(it.key()))it=delegate->previews.erase(it);else ++it;
    }
    list->clear();int selectedRow=-1;
    for(const auto &item:delegate->entries) {
        const QSize imageSize=delegate->previews.value(item.id).original;
        auto *row=new QListWidgetItem(list);row->setData(Qt::UserRole,item.id);row->setText(imageSize.isEmpty()?cleanText(item.text):QStringLiteral("Imagen %1 × %2").arg(imageSize.width()).arg(imageSize.height()));
        if(item.id==selected)selectedRow=list->count()-1;
    }
    if(list->count())list->setCurrentRow(selectNewest?0:selectedRow>=0?selectedRow:qBound(0,oldRow,list->count()-1));
    const bool hasItems=!delegate->entries.isEmpty();list->setVisible(hasItems);empty->setVisible(!hasItems);empty->setText(search->text().isEmpty()?QStringLiteral("No hay nada copiado"):QStringLiteral("Sin resultados"));
    clearButton->setVisible(history->hasItems());
    layout()->activate();updateCardSizes();
    if(!selectNewest)list->verticalScrollBar()->setValue(scroll);
    update();
}
void Panel::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setBrush(palette().color(QPalette::Window));p.setPen(mix(palette().color(QPalette::Window),palette().color(QPalette::WindowText),.20));p.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),10,10);
}
bool Panel::handleKey(QKeyEvent *e) {
    if(e->key()==Qt::Key_Escape) {
        if(authorizationPending){finishAuthorization(false);return true;}
        if(!search->text().isEmpty()){search->clear();search->hide();header->show();list->setFocus();}else emit dismissRequested(true);return true;
    }
    if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter){if(!currentId().isEmpty())emit chosen(currentId());return true;}
    if(e->key()==Qt::Key_Down||e->key()==Qt::Key_Up){if(list->count())list->setCurrentRow(qBound(0,list->currentRow()+(e->key()==Qt::Key_Down?1:-1),list->count()-1));return true;}
    if(e->key()==Qt::Key_Delete && search->text().isEmpty()){if(!currentId().isEmpty()&&history->remove(currentId()))refresh();return true;}
    if(e->key()==Qt::Key_P && e->modifiers()==Qt::ControlModifier){if(!currentId().isEmpty()&&history->pin(currentId()))refresh();return true;}
    if(e->key()==Qt::Key_Backspace&&search->isHidden()){return true;}
    if(search->isHidden()&&!e->text().isEmpty()&&e->text().at(0).isPrint() && !(e->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))) {
        header->hide();search->show();search->setFocus();search->setText(e->text());return true;
    }
    return false;
}
void Panel::keyPressEvent(QKeyEvent *e) { if(!handleKey(e))QWidget::keyPressEvent(e); }
bool Panel::eventFilter(QObject *obj,QEvent *e) {
    if(obj==list->viewport()&&e->type()==QEvent::Resize)queueCardSizes();
    if(e->type()==QEvent::MouseMove||e->type()==QEvent::MouseButtonPress||e->type()==QEvent::MouseButtonRelease) {
        auto *mouse=static_cast<QMouseEvent*>(e);auto *widget=qobject_cast<QWidget*>(obj);
        const auto edges=resizeEdges(widget->mapTo(this,mouse->position().toPoint()));
        if(e->type()==QEvent::MouseMove) {
            Qt::CursorShape cursor=Qt::ArrowCursor;
            if(edges.testFlag(Qt::LeftEdge)||edges.testFlag(Qt::RightEdge))cursor=(edges.testFlag(Qt::TopEdge)||edges.testFlag(Qt::BottomEdge))?((edges.testFlag(Qt::LeftEdge)==edges.testFlag(Qt::TopEdge))?Qt::SizeFDiagCursor:Qt::SizeBDiagCursor):Qt::SizeHorCursor;
            else if(edges)cursor=Qt::SizeVerCursor;
            setCursor(cursor);widget->setCursor(cursor);
        }
        if(e->type()==QEvent::MouseButtonPress)resizing=false;
        if(e->type()==QEvent::MouseButtonPress&&mouse->button()==Qt::LeftButton&&edges) {
            resizing=windowHandle()&&windowHandle()->startSystemResize(edges);return true;
        }
        if(e->type()==QEvent::MouseButtonRelease&&(resizing||edges)) {resizing=false;return true;}
    }
    if(obj==clearButton&&e->type()==QEvent::KeyPress&&(static_cast<QKeyEvent*>(e)->key()==Qt::Key_Return||static_cast<QKeyEvent*>(e)->key()==Qt::Key_Enter))return true;
    if(e->type()==QEvent::KeyPress&&handleKey(static_cast<QKeyEvent*>(e)))return true;
    if(obj==list->viewport()&&e->type()==QEvent::MouseButtonRelease) {
        auto *mouse=static_cast<QMouseEvent*>(e);if(mouse->button()!=Qt::LeftButton)return false;
        const auto index=list->indexAt(mouse->position().toPoint());if(!index.isValid())return false;
        const QRect card=list->visualRect(index).adjusted(0,0,-1,index.row()==list->count()-1?0:-6);if(!card.contains(mouse->position().toPoint()))return true;
        const QString id=list->item(index.row())->data(Qt::UserRole).toString();
        if(actionRect(card,false).contains(mouse->position().toPoint())){if(history->remove(id))refresh();}
        else if(actionRect(card,true).contains(mouse->position().toPoint())){if(history->pin(id))refresh();}
        else emit chosen(id);
        return true;
    }
    if(obj==list->viewport()&&e->type()==QEvent::ToolTip){
        auto *help=static_cast<QHelpEvent*>(e);const auto index=list->indexAt(help->pos());
        if(index.isValid()) {
            const QRect card=list->visualRect(index).adjusted(0,0,-1,index.row()==list->count()-1?0:-6);
            QString tip;
            if(actionRect(card,false).contains(help->pos()))tip=QStringLiteral("Eliminar");
            else if(actionRect(card,true).contains(help->pos()))tip=static_cast<CardDelegate*>(list->itemDelegate())->entries[index.row()].pinned?QStringLiteral("Quitar de fijados"):QStringLiteral("Fijar");
            if(!tip.isEmpty()){QToolTip::showText(help->globalPos(),tip,list);return true;}
        }
        QToolTip::hideText();return true;
    }
    return QWidget::eventFilter(obj,e);
}
bool Panel::event(QEvent *e) {
    if(e->type()==QEvent::MouseMove||e->type()==QEvent::MouseButtonPress||e->type()==QEvent::MouseButtonRelease) {
        if(eventFilter(this,e))return true;
    }
    if(e->type()==QEvent::Hide&&authorizationPending)finishAuthorization(false);
    if(e->type()==QEvent::WindowDeactivate&&isVisible()&&!authorizationPending)emit dismissRequested(false);
    return QWidget::event(e);
}
}
