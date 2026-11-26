#include "panel.h"
#include <QApplication>
#include <QButtonGroup>
#include <QToolButton>
#include <QLocale>
#include <cmath>
#include <QAction>
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
    } else if (kind==QStringLiteral("gear")) {
        QPolygonF points;for(int i=0;i<8;++i)for(const auto &v:QList<QPointF>{{4.9,-14},{6.6,-8},{6.6,8},{4.9,14}}) {
            const double a=(45*i+v.y())*M_PI/180;points<<QPointF(8+v.x()*std::cos(a),8+v.x()*std::sin(a));
        }
        p.drawPolygon(points);p.drawEllipse(QPointF(8,8),2.1,2.1);
    } else if (kind==QStringLiteral("back")) {
        p.drawLine(QPointF(13,8),QPointF(3,8));p.drawPolyline(QPolygonF{{7.5,3.5},{3,8},{7.5,12.5}});
    } else if (kind==QStringLiteral("check")) {
        p.setPen(QPen(color,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPolyline(QPolygonF{{3,8.5},{6.5,12},{13,4.5}});
    } else if (kind==QStringLiteral("warning")) {
        p.drawPolygon(QPolygonF{{8,2},{14.5,13.5},{1.5,13.5}});p.drawLine(QPointF(8,6.5),QPointF(8,9.5));p.drawPoint(QPointF(8,11.6));
    } else if (kind==QStringLiteral("search")) {
        p.drawEllipse(QPointF(6.8,6.8),4.6,4.6);p.drawLine(QPointF(10.2,10.2),QPointF(14,14));
    } else {
        p.drawLine(QPointF(6,10),QPointF(10,6)); p.drawArc(QRectF(7,1,7,7),-45*16,270*16); p.drawArc(QRectF(2,8,7,7),135*16,270*16);
    }
    p.restore();
}
static QIcon buttonIcon(const QString &kind, QColor color) {
    QPixmap pix(32,32);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);icon(p,{},kind,color);return QIcon(pix);
}
class StatusLabel final : public QLabel {
public:
    using QLabel::QLabel;
    void setGlyph(const QString &value) { glyph=value;setIndent(glyph.isEmpty()?0:22);update(); }
protected:
    void paintEvent(QPaintEvent *event) override {
        QLabel::paintEvent(event);
        if(glyph.isEmpty())return;
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        icon(p,{0,1},glyph,glyph==QStringLiteral("warning")?QColor("#da4453"):palette().color(QPalette::Highlight));
    }
private:
    QString glyph;
};
static constexpr int firstRowInset=8;
static QRect cardRect(QRect row, int index, int count) { return row.adjusted(0,index==0?firstRowInset:0,-1,index==count-1?0:-6); }
static QRect actionRect(const QRect &card, bool pin) { return QRect(card.right()-(pin?31:61),card.top()+4,28,28); }
static int cardHeight(const Item &item, QSize imageSize, int width, const QFont &font, bool lastChosen) {
    if (!imageSize.isEmpty()) return imageSize.scaled(QSize(qMax(1,width-112),200),Qt::KeepAspectRatio).height()+20;
    if (isLink(cleanText(item.text))) return 40;
    QTextLayout text(cleanText(item.text).replace(QChar('\n'),QChar::LineSeparator),font); QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); text.setTextOption(option);
    text.beginLayout(); int lines=0;
    while (lines<3) { auto line=text.createLine(); if(!line.isValid()) break; line.setLineWidth(qMax(20,width-24-(lastChosen?(item.pinned?116:96):(item.pinned?24:0)))); ++lines; }
    text.endLayout(); return qMax(40,lines*QFontMetrics(font).lineSpacing()+22);
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
        if(text()==QStringLiteral("Sin resultados")) {p.drawEllipse(QPointF(21,21),11,11);p.drawLine(QPointF(29,29),QPointF(38,38));}
        else {p.drawRoundedRect(QRectF(11,9,26,33),3,3);p.setBrush(palette().color(QPalette::Window));p.drawRoundedRect(QRectF(18,5.5,12,7),2,2);
        p.drawLine(QPointF(17,21),QPointF(31,21));p.drawLine(QPointF(17,27),QPointF(31,27));p.drawLine(QPointF(17,33),QPointF(26,33));}p.restore();
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
        const QRect card=cardRect(o.rect,index.row(),entries.size());
        const auto &palette=theme;
        const QColor fg=palette.color(QPalette::Text), highlight=palette.color(QPalette::Highlight), muted=palette.color(QPalette::Disabled,QPalette::Text);
        const bool selected=o.state & QStyle::State_Selected, hover=o.state & QStyle::State_MouseOver;
        QColor bg=palette.color(QPalette::Button);
        if(selected||hover) bg=mix(bg,highlight,selected?.25:.12);
        p->save(); p->setRenderHint(QPainter::Antialiasing); p->setBrush(bg);
        p->setPen(QPen(selected?highlight:hover?mix(bg,highlight,.45):mix(bg,fg,.12),selected?2:1));
        p->drawRoundedRect(QRectF(card).adjusted(1,1,-1,-1),6,6);
        const auto preview=previews.value(item.id);const int padding=preview.original.isEmpty()?11:10;
        QRect content=card.adjusted(12,padding,-12,-padding);
        const bool lastChosen=item.id==last;
        const int reserve=lastChosen?(item.pinned?116:96):(item.pinned?24:0);
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
            if(count==1)content.moveTop(card.top()+(card.height()-y)/2);
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
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,8);layout->setSpacing(0);
    header=new QWidget(this);header->setFixedHeight(89);auto *band=new QVBoxLayout(header);band->setContentsMargins(8,8,8,9);band->setSpacing(6);
    auto *titleRow=new QWidget(header);titleRow->setFixedHeight(32);auto *head=new QHBoxLayout(titleRow);head->setContentsMargins(12,0,0,0);band->addWidget(titleRow);
    backButton=new QToolButton(titleRow);backButton->setObjectName(QStringLiteral("back"));backButton->setFixedSize(28,28);backButton->setAutoRaise(true);backButton->setToolTip(QStringLiteral("Volver (Esc)"));backButton->setAccessibleName(QStringLiteral("Volver"));backButton->hide();head->addWidget(backButton);
    title=new QLabel(QStringLiteral("Portapapeles"));QFont bold=f;bold.setWeight(QFont::DemiBold);title->setFont(bold);head->addWidget(title);head->addStretch();
    settingsButton=new QToolButton(titleRow);settingsButton->setObjectName(QStringLiteral("settings"));settingsButton->setFixedSize(28,28);settingsButton->setAutoRaise(true);settingsButton->setToolTip(QStringLiteral("Ajustes (Ctrl+,)"));settingsButton->setAccessibleName(QStringLiteral("Ajustes"));settingsButton->setFocusPolicy(Qt::NoFocus);head->addWidget(settingsButton);layout->addWidget(header);
    search=new QLineEdit(this);search->setObjectName(QStringLiteral("filter"));search->setAccessibleName(QStringLiteral("Filtrar portapapeles"));search->setClearButtonEnabled(true);search->setFixedHeight(34);search->setPlaceholderText(QStringLiteral("Buscar…"));search->setFocusPolicy(Qt::StrongFocus);searchIcon=search->addAction(QIcon(),QLineEdit::LeadingPosition);band->addWidget(search);setFocusProxy(search);
    listPage=new QWidget(this);auto *body=new QVBoxLayout(listPage);body->setContentsMargins(8,0,8,0);body->setSpacing(0);layout->addWidget(listPage,1);
    list=new QListWidget(this);list->setObjectName(QStringLiteral("history"));list->setFrameShape(QFrame::NoFrame);list->setMouseTracking(true);list->setSelectionMode(QAbstractItemView::SingleSelection);list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);list->setFocusPolicy(Qt::NoFocus);list->setItemDelegate(new CardDelegate(list));list->viewport()->setAttribute(Qt::WA_Hover);list->viewport()->installEventFilter(this);list->installEventFilter(this);list->setAccessibleName(QStringLiteral("Historial, más reciente primero"));
    body->addWidget(list,1);list->verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    empty=new EmptyLabel(this);empty->setAlignment(Qt::AlignCenter);empty->setMinimumHeight(152);body->addWidget(empty,1);
    settingsPage=new QWidget(this);settingsPage->setObjectName(QStringLiteral("settingsPage"));settingsPage->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Ignored);layout->addWidget(settingsPage,1);settingsPage->hide();
    auto *settingsLayout=new QVBoxLayout(settingsPage);settingsLayout->setContentsMargins(20,16,20,12);settingsLayout->setSpacing(8);
    auto *label=new QLabel(QStringLiteral("Máximo de tarjetas"));settingsLayout->addWidget(label);
    presets=new QButtonGroup(this);auto *choices=new QHBoxLayout;choices->setSpacing(6);
    for(int value:{50,100,200,500}) {
        auto *button=new QPushButton(QString::number(value));button->setObjectName(QStringLiteral("limit%1").arg(value));button->setCheckable(true);button->setAutoDefault(false);button->setFixedSize(58,32);presets->addButton(button,value);choices->addWidget(button);
    }
    choices->addStretch();settingsLayout->addLayout(choices);
    auto makeLabel=[&](const QString &name) {auto *label=new StatusLabel;label->setObjectName(name);label->setWordWrap(true);return label;};
    countLabel=makeLabel(QStringLiteral("historyUsage"));settingsLayout->addWidget(countLabel);
    warningLabel=makeLabel(QStringLiteral("retentionWarning"));static_cast<StatusLabel*>(warningLabel)->setGlyph(QStringLiteral("warning"));settingsLayout->addWidget(warningLabel);
    auto *saveRow=new QHBoxLayout;saveRow->setSpacing(12);saveButton=new QPushButton(QStringLiteral("Guardar"));saveButton->setObjectName(QStringLiteral("saveLimit"));saveButton->setFixedHeight(30);saveButton->setAutoDefault(false);saveRow->addWidget(saveButton);saveStatus=makeLabel(QStringLiteral("saveStatus"));saveRow->addWidget(saveStatus,1);settingsLayout->addLayout(saveRow);
    settingsLayout->addSpacing(12);divider=new QLabel;divider->setFixedHeight(1);settingsLayout->addWidget(divider);settingsLayout->addSpacing(12);
    auto *clearRow=new QHBoxLayout;clearRow->setSpacing(12);clearButton=new QPushButton(QStringLiteral("Borrar todo"));clearButton->setObjectName(QStringLiteral("clearAll"));clearButton->setFixedHeight(30);clearButton->setAutoDefault(false);clearRow->addWidget(clearButton);clearStatus=makeLabel(QStringLiteral("clearStatus"));clearRow->addWidget(clearStatus,1);settingsLayout->addLayout(clearRow);settingsLayout->addStretch();
    QFont small=f;small.setPointSize(9);saveStatus->setFont(small);clearStatus->setFont(small);
    connect(presets,&QButtonGroup::idToggled,this,[this](int value,bool checked){if(!checked)return;selectedLimit=value;saveStatus->clear();clearStatus->clear();static_cast<StatusLabel*>(saveStatus)->setGlyph({});updateSettings();});
    connect(settingsButton,&QToolButton::clicked,this,[this]{showSettings(true);});connect(backButton,&QToolButton::clicked,this,[this]{showSettings(false);});
    connect(clearButton,&QPushButton::clicked,this,&Panel::requestClear);connect(saveButton,&QPushButton::clicked,this,&Panel::requestSave);
    QWidget::setTabOrder(presets->button(50),presets->button(100));QWidget::setTabOrder(presets->button(100),presets->button(200));QWidget::setTabOrder(presets->button(200),presets->button(500));QWidget::setTabOrder(presets->button(500),saveButton);QWidget::setTabOrder(saveButton,clearButton);QWidget::setTabOrder(clearButton,backButton);
    applyStyle();
    connect(search,&QLineEdit::textChanged,this,[this]{refresh(true);});
    for(auto *child:findChildren<QWidget*>()) { child->setMouseTracking(true);child->installEventFilter(this); }
    refresh(true);
}
void Panel::applyStyle() {
    const auto window=palette().color(QPalette::Window),text=palette().color(QPalette::WindowText);
    QPalette ep=palette();ep.setColor(QPalette::WindowText,palette().color(QPalette::Disabled,QPalette::Text));empty->setPalette(ep);
    QPixmap glyph(32,32);glyph.setDevicePixelRatio(2);glyph.fill(Qt::transparent);QPainter painter(&glyph);painter.setRenderHint(QPainter::Antialiasing);icon(painter,{},QStringLiteral("search"),palette().color(QPalette::PlaceholderText));painter.end();searchIcon->setIcon(QIcon(glyph));
    // Breeze's native checked push button is gray; make the saved/selected preset clear.
    const auto highlight=palette().color(QPalette::Highlight);
    for(auto *button:presets->buttons())button->setStyleSheet(QStringLiteral("QPushButton:enabled:checked { background: %1; border: 1px solid %2; border-radius: 4px; } QPushButton:enabled:checked:focus { border: 2px solid %2; }").arg(mix(palette().color(QPalette::Button),highlight,.25).name(),highlight.name()));
    settingsButton->setIcon(buttonIcon(QStringLiteral("gear"),text));backButton->setIcon(buttonIcon(QStringLiteral("back"),text));clearButton->setIcon(buttonIcon(QStringLiteral("delete"),text));
    QPalette muted=palette();muted.setColor(QPalette::WindowText,palette().color(QPalette::Disabled,QPalette::Text));countLabel->setPalette(muted);
    divider->setAutoFillBackground(true);QPalette line=palette();line.setColor(QPalette::Window,mix(window,text,.14));divider->setPalette(line);
    list->setStyleSheet(QStringLiteral(
        "QScrollBar:vertical { width: 14px; background: transparent; margin: 0; }"
        "QScrollBar:vertical:hover { background: %1; border-radius: 7px; }"
        "QScrollBar::handle:vertical { min-height: 32px; margin: 0 4px; border-radius: 3px; background: %2; }"
        "QScrollBar::handle:vertical:hover { margin: 0 2px; border-radius: 5px; background: %3; }"
        "QScrollBar::handle:vertical:pressed { margin: 0 2px; border-radius: 5px; background: %4; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; background: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(mix(window,text,.08).name(),mix(window,text,.35).name(),mix(window,text,.55).name(),palette().color(QPalette::Highlight).name()));
    // Qt stylesheets retain a separate viewport palette across theme changes.
    QPalette lp=palette();lp.setColor(QPalette::Base,window);list->setPalette(lp);list->viewport()->setPalette(lp);
}
QString Panel::currentId() const { auto *item=list->currentItem();return item?item->data(Qt::UserRole).toString():QString(); }
QString Panel::filter() const { return search->text(); }
void Panel::prepareOpen(const QRect &availableArea) {
    if(settingsVisible)showSettings(false);
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
    search->clear();refresh(true);search->setFocus();
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
    if(!list||settingsVisible)return;
    auto *delegate=static_cast<CardDelegate*>(list->itemDelegate());
    const int width=qMax(1,list->viewport()->width());
    int height=0;
    for(int row=0;row<list->count();++row) {
        const auto &item=delegate->entries[row];
        const int rowHeight=cardHeight(item,delegate->previews.value(item.id).original,width-1,font(),item.id==delegate->last)+(row==list->count()-1?0:6)+(row==0?firstRowInset:0);
        list->item(row)->setSizeHint(QSize(width,rowHeight));height+=rowHeight;
    }
    if(!customSize)resizeInternally(QSize(width==1?560:this->width(),qMin(760,header->height()+layout()->contentsMargins().bottom()+(list->count()?qMax(40,height):empty->minimumHeight()+firstRowInset))));
    list->doItemsLayout();list->viewport()->update();
}
void Panel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    const bool automatic=adjustingSize||event->size()==pendingAutomaticSize;
    pendingAutomaticSize={}; // Suppress only the pending internal resize, not this size forever.
    if(isVisible()&&!automatic) {
        customSize=true;preferredSize=event->size();if(settingsVisible)listSize=event->size();
        QSettings settings(settingsFile,QSettings::IniFormat);
        settings.setValue(QStringLiteral("Window/size"),preferredSize);settings.sync();
    }
    if(settingsVisible)growSettings();else queueCardSizes();
}
Qt::Edges Panel::resizeEdges(const QPoint &position) const {
    Qt::Edges edges;
    if(position.x()<6)edges|=Qt::LeftEdge;
    else if(position.x()>=width()-6)edges|=Qt::RightEdge;
    if(position.y()<6)edges|=Qt::TopEdge;
    else if(position.y()>=height()-6)edges|=Qt::BottomEdge;
    return edges;
}
void Panel::showSettings(bool show) {
    if(authorizationPending||show==settingsVisible)return;
    settingsVisible=show;
    if(show)listSize=size();
    title->setText(show?QStringLiteral("Ajustes"):QStringLiteral("Portapapeles"));
    auto *head=qobject_cast<QHBoxLayout*>(title->parentWidget()->layout());head->setContentsMargins(show?0:12,0,0,0);head->setSpacing(show?6:0);
    header->setFixedHeight(show?49:89);search->setVisible(!show);settingsButton->setVisible(!show);backButton->setVisible(show);settingsPage->setVisible(show);
    listPage->setVisible(!show);setFocusProxy(show?nullptr:search);setFocusPolicy(show?Qt::ClickFocus:Qt::StrongFocus);
    saveStatus->clear();clearStatus->clear();static_cast<StatusLabel*>(saveStatus)->setGlyph({});
    selectedLimit=history->maxUnpinned();
    if(show) {
        updateSettings();list->hide();empty->hide();layout()->activate();growSettings();presets->checkedButton()->setFocus();
    } else {
        resizeInternally(listSize);refresh();search->setFocus();
    }
    update();
}
void Panel::growSettings() {
    if(!settingsVisible)return;
    const int needed=header->height()+settingsPage->layout()->totalHeightForWidth(width())+layout()->contentsMargins().bottom();
    if(height()<needed)resizeInternally(QSize(width(),needed));
}
void Panel::updateSettings() {
    const auto usage=history->usage();const int saved=history->maxUnpinned();
    const QSignalBlocker blocker(presets);presets->button(selectedLimit)->setChecked(true);
    for(auto *button:presets->buttons()) {button->setEnabled(!authorizationPending);button->setFocusPolicy(button->isChecked()?Qt::StrongFocus:Qt::ClickFocus);}
    backButton->setEnabled(!authorizationPending);clearButton->setEnabled(!authorizationPending&&usage.unpinned>0);
    saveButton->setEnabled(!authorizationPending&&selectedLimit!=saved);saveButton->setDefault(saveButton->isEnabled());
    QString count=usage.unpinned==1?QStringLiteral("1 tarjeta"):QStringLiteral("%1 tarjetas").arg(usage.unpinned);
    if(usage.pinned)count+=usage.pinned==1?QStringLiteral(" y 1 fijada"):QStringLiteral(" y %1 fijadas").arg(usage.pinned);
    countLabel->setText(usage.unpinned||usage.pinned?QStringLiteral("Ahora: %1 · %2").arg(count,QLocale().formattedDataSize(usage.bytes,0,QLocale::DataSizeTraditionalFormat)):QStringLiteral("Ahora no hay tarjetas."));
    const int removed=qMax(0,usage.unpinned-selectedLimit);
    warningLabel->setVisible(removed>0);
    warningLabel->setText((removed==1?QStringLiteral("Se borrará la tarjeta más antigua"):QStringLiteral("Se borrarán las %1 tarjetas más antiguas").arg(removed))+(usage.pinned?QStringLiteral("; los fijados se conservan."):QStringLiteral(".")));
    // Recompute idle texts so they track the selection and live usage; outcomes stay.
    if(saveStatus->text().startsWith(QStringLiteral("De ")))saveStatus->clear();
    if(clearStatus->text().startsWith(QStringLiteral("Solo "))||clearStatus->text()==QStringLiteral("No hay tarjetas para borrar."))clearStatus->clear();
    if(saveStatus->text().isEmpty()&&selectedLimit!=saved)saveStatus->setText(QStringLiteral("De %1 a %2").arg(saved).arg(selectedLimit));
    if(clearStatus->text().isEmpty())clearStatus->setText(usage.unpinned?QStringLiteral("Solo en este panel; conserva los fijados."):QStringLiteral("No hay tarjetas para borrar."));
    auto statusPalette=[&](QLabel *label,bool neutral) {QPalette p=palette();if(neutral)p.setColor(QPalette::WindowText,palette().color(QPalette::Disabled,QPalette::Text));label->setPalette(p);};
    statusPalette(saveStatus,saveStatus->text().startsWith(QStringLiteral("De "))||saveStatus->text()==QStringLiteral("Esperando la contraseña…"));
    statusPalette(clearStatus,clearStatus->text().startsWith(QStringLiteral("Solo "))||clearStatus->text()==QStringLiteral("No hay tarjetas para borrar.")||clearStatus->text()==QStringLiteral("Esperando la contraseña…"));
    if(settingsVisible){settingsPage->layout()->invalidate();layout()->activate();growSettings();}
}
void Panel::requestClear() {if(clearButton->isEnabled())requestAuthorization(false);}
void Panel::requestSave() {if(saveButton->isEnabled())requestAuthorization(true);}
void Panel::requestAuthorization(bool saving) {
    if(authorizationPending)return;
    savingLimit=saving;authorizationPending=true;
    saveStatus->clear();clearStatus->clear();static_cast<StatusLabel*>(saveStatus)->setGlyph({});
    (saving?saveStatus:clearStatus)->setText(QStringLiteral("Esperando la contraseña…"));updateSettings();setFocus();
    authorization=new QProcess(this);
    connect(authorization,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){finishAuthorization(status==QProcess::NormalExit?code:1);});
    connect(authorization,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finishAuthorization(1);});
    // Include start time and uid to bind polkit's subject to this exact process.
    QFile stat(QStringLiteral("/proc/self/stat"));
    if(!stat.open(QIODevice::ReadOnly)){finishAuthorization(1);return;}
    const QByteArray fields=stat.readAll();const auto values=fields.mid(fields.lastIndexOf(')')+2).split(' ');
    if(values.size()<=19){finishAuthorization(1);return;}
    const QString subject=QStringLiteral("%1,%2,%3").arg(QCoreApplication::applicationPid()).arg(QString::fromLatin1(values[19])).arg(::getuid());
    auto *timeout=new QTimer(authorization);timeout->setSingleShot(true);
    connect(timeout,&QTimer::timeout,this,[this]{if(authorizationPending)finishAuthorization(1);});timeout->start(120000);
    authorization->start(authorizationProgram,{QStringLiteral("--action-id"),saving?QStringLiteral("org.local.clipboard-panel.change-limit"):QStringLiteral("org.local.clipboard-panel.clear-history"),QStringLiteral("--process"),subject,QStringLiteral("--allow-user-interaction")});
}
void Panel::finishAuthorization(int result) {
    if(!authorizationPending)return;
    authorizationPending=false;
    auto *process=authorization;authorization=nullptr;
    process->disconnect(this);process->setParent(qApp);
    if(auto *timeout=process->findChild<QTimer*>())timeout->stop();
    if(process->state()!=QProcess::NotRunning)process->kill();
    // Reap asynchronously; never wait for a child in the GUI thread.
    if(process->state()==QProcess::NotRunning)process->deleteLater();
    else connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
    if(savingLimit) {
        const int removed=result==0?history->setMaxUnpinned(selectedLimit):-1;
        selectedLimit=history->maxUnpinned();
        if(result==0&&removed>=0) {
            saveStatus->setText(removed==0?QStringLiteral("Guardado."):removed==1?QStringLiteral("Guardado; se borró 1."):QStringLiteral("Guardado; se borraron %1.").arg(removed));static_cast<StatusLabel*>(saveStatus)->setGlyph(QStringLiteral("check"));
        } else saveStatus->setText((result==0||result==3?QStringLiteral("No se guardó; sigue en %1."):QStringLiteral("No se pudo autorizar; sigue en %1.")).arg(selectedLimit));
    } else {
        const bool cleared=result==0&&history->clear();
        if(cleared)clearStatus->setText(history->usage().pinned?QStringLiteral("Borrado; los fijados se conservan."):QStringLiteral("Borrado."));
        else {selectedLimit=history->maxUnpinned();clearStatus->setText(result==0||result==3?QStringLiteral("No se borró."):QStringLiteral("No se pudo autorizar."));}
    }
    refresh(true);updateSettings();
    if(!savingLimit&&clearButton->isEnabled())clearButton->setFocus();else presets->checkedButton()->setFocus();
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
    if(settingsVisible){list->hide();empty->hide();updateSettings();}
    layout()->activate();updateCardSizes();
    if(!selectNewest)list->verticalScrollBar()->setValue(scroll);
    update();
}
void Panel::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QPainterPath outline;outline.addRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),10,10);
    const auto window=palette().color(QPalette::Window),text=palette().color(QPalette::WindowText);
    p.setClipPath(outline);p.fillRect(rect(),window);p.fillRect(QRect(0,0,width(),header->height()-1),mix(window,text,.05));p.fillRect(QRect(0,header->height()-1,width(),1),mix(window,text,.14));
    p.setClipping(false);p.setBrush(Qt::NoBrush);p.setPen(mix(window,text,.20));p.drawPath(outline);
}
bool Panel::handleKey(QKeyEvent *e) {
    if(e->key()==Qt::Key_Escape) {
        if(authorizationPending){finishAuthorization(3);return true;}
        if(settingsVisible){showSettings(false);return true;}
        if(!search->text().isEmpty()){search->clear();}else emit dismissRequested(true);return true;
    }
    if(settingsVisible) {
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter) {
            if(!authorizationPending){if(clearButton->hasFocus())requestClear();else if(saveButton->isEnabled()&&(saveButton->hasFocus()||presets->buttons().contains(qobject_cast<QAbstractButton*>(focusWidget()))))requestSave();}return true;
        }
        return false;
    }
    if((e->key()==Qt::Key_Comma&&e->modifiers()==Qt::ControlModifier)||e->matches(QKeySequence::Preferences)){showSettings(true);return true;}
    if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter){if(!currentId().isEmpty())emit chosen(currentId());return true;}
    if(e->key()==Qt::Key_Down||e->key()==Qt::Key_Up){if(list->count())list->setCurrentRow(qBound(0,list->currentRow()+(e->key()==Qt::Key_Down?1:-1),list->count()-1));if(list->currentItem())list->scrollToItem(list->currentItem());return true;}
    if((e->key()==Qt::Key_Home||e->key()==Qt::Key_End)&&search->text().isEmpty()){if(list->count()){list->setCurrentRow(e->key()==Qt::Key_Home?0:list->count()-1);list->scrollToItem(list->currentItem());}return true;}
    if(e->key()==Qt::Key_Tab||e->key()==Qt::Key_Backtab)return true;
    if(e->key()==Qt::Key_Delete && search->text().isEmpty()){if(!currentId().isEmpty()&&history->remove(currentId()))refresh();return true;}
    if(e->key()==Qt::Key_P && e->modifiers()==Qt::ControlModifier){if(!currentId().isEmpty()&&history->pin(currentId()))refresh();return true;}
    return false;
}
void Panel::keyPressEvent(QKeyEvent *e) { if(!handleKey(e))QWidget::keyPressEvent(e); }
bool Panel::eventFilter(QObject *obj,QEvent *e) {
    if(obj==list->viewport()&&e->type()==QEvent::Resize)queueCardSizes();
    if(e->type()==QEvent::MouseMove||e->type()==QEvent::MouseButtonPress||e->type()==QEvent::MouseButtonRelease) {
        auto *mouse=static_cast<QMouseEvent*>(e);auto *widget=qobject_cast<QWidget*>(obj);
        const auto position=widget->mapTo(this,mouse->position().toPoint());const auto edges=resizeEdges(position);
        if(e->type()==QEvent::MouseMove) {
            Qt::CursorShape cursor=Qt::ArrowCursor;
            if(edges.testFlag(Qt::LeftEdge)||edges.testFlag(Qt::RightEdge))cursor=(edges.testFlag(Qt::TopEdge)||edges.testFlag(Qt::BottomEdge))?((edges.testFlag(Qt::LeftEdge)==edges.testFlag(Qt::TopEdge))?Qt::SizeFDiagCursor:Qt::SizeBDiagCursor):Qt::SizeHorCursor;
            else if(edges)cursor=Qt::SizeVerCursor;
            setCursor(cursor);widget->setCursor(cursor);
        }
        if(e->type()==QEvent::MouseButtonPress)resizing=false;
        if(e->type()==QEvent::MouseButtonPress&&mouse->button()==Qt::LeftButton&&edges&&obj!=list->verticalScrollBar()) {
            resizing=windowHandle()&&windowHandle()->startSystemResize(edges);return true;
        }
        if(e->type()==QEvent::MouseButtonPress&&mouse->button()==Qt::LeftButton&&!edges&&header->geometry().contains(position)&&!(search->isVisible()&&search->rect().contains(search->mapFrom(this,position)))&&!(settingsButton->isVisible()&&settingsButton->rect().contains(settingsButton->mapFrom(this,position)))&&!(backButton->isVisible()&&backButton->rect().contains(backButton->mapFrom(this,position)))) {
            if(windowHandle())windowHandle()->startSystemMove();return true;
        }
        if(e->type()==QEvent::MouseButtonRelease&&resizing) {resizing=false;return true;}
    }
    if(obj==search&&e->type()==QEvent::KeyPress) {
        auto *key=static_cast<QKeyEvent*>(e);
        if(key->key()==Qt::Key_PageUp||key->key()==Qt::Key_PageDown){QKeyEvent copy(key->type(),key->key(),key->modifiers(),key->text(),key->isAutoRepeat(),key->count());QCoreApplication::sendEvent(list,&copy);return true;}
    }
    if(e->type()==QEvent::KeyPress&&handleKey(static_cast<QKeyEvent*>(e)))return true;
    if(obj==list->viewport()&&e->type()==QEvent::MouseButtonRelease) {
        auto *mouse=static_cast<QMouseEvent*>(e);if(mouse->button()!=Qt::LeftButton)return false;
        const auto index=list->indexAt(mouse->position().toPoint());if(!index.isValid())return false;
        const QRect card=cardRect(list->visualRect(index),index.row(),list->count());if(!card.contains(mouse->position().toPoint()))return true;
        const QString id=list->item(index.row())->data(Qt::UserRole).toString();
        if(actionRect(card,false).contains(mouse->position().toPoint())){if(history->remove(id))refresh();}
        else if(actionRect(card,true).contains(mouse->position().toPoint())){if(history->pin(id))refresh();}
        else emit chosen(id);
        return true;
    }
    if(obj==list->viewport()&&e->type()==QEvent::ToolTip){
        auto *help=static_cast<QHelpEvent*>(e);const auto index=list->indexAt(help->pos());
        if(index.isValid()) {
            const QRect card=cardRect(list->visualRect(index),index.row(),list->count());
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
    if((e->type()==QEvent::PaletteChange||e->type()==QEvent::ApplicationPaletteChange)&&empty){applyStyle();refresh();}
    if(e->type()==QEvent::MouseMove||e->type()==QEvent::MouseButtonPress||e->type()==QEvent::MouseButtonRelease) {
        if(eventFilter(this,e))return true;
    }
    if(e->type()==QEvent::Hide&&authorizationPending)finishAuthorization(3);
    if(e->type()==QEvent::WindowDeactivate&&isVisible()&&!authorizationPending)emit dismissRequested(false);
    return QWidget::event(e);
}
}
