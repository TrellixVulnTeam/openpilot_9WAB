#include "selfdrive/ui/qt/offroad/networking.h"

#include <algorithm>

#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>

#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/qt_window.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"


// Networking functions

Networking::Networking(QWidget* parent, bool show_advanced) : QFrame(parent) {
  main_layout = new QStackedLayout(this);

  wifi = new WifiManager(this);
  connect(wifi, &WifiManager::refreshSignal, this, &Networking::refresh);
  connect(wifi, &WifiManager::wrongPassword, this, &Networking::wrongPassword);

  wifiScreen = new QWidget(this);
  QVBoxLayout* vlayout = new QVBoxLayout(wifiScreen);
  vlayout->setContentsMargins(20, 20, 20, 20);
  if (show_advanced) {
    QPushButton* advancedSettings = new QPushButton(tr("Advanced"));
    advancedSettings->setObjectName("advanced_btn");
    advancedSettings->setStyleSheet("margin-right: 30px;");
    advancedSettings->setFixedSize(400, 100);
    connect(advancedSettings, &QPushButton::clicked, [=]() { main_layout->setCurrentWidget(an); });
    vlayout->addSpacing(10);
    vlayout->addWidget(advancedSettings, 0, Qt::AlignRight);
    vlayout->addSpacing(10);
  }

  wifiWidget = new WifiUI(wifi, this);
  wifiWidget->setObjectName("wifiWidget");
  connect(wifiWidget, &WifiUI::connectToNetwork, this, &Networking::connectToNetwork);
  connect(wifiWidget, &WifiUI::viewNetwork, this, &Networking::viewNetwork);

  ScrollView *wifiScroller = new ScrollView(wifiWidget, this);
  wifiScroller->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  vlayout->addWidget(wifiScroller, 1);
  main_layout->addWidget(wifiScreen);

  detailsWidget = new WifiDetails(wifi, this);
  detailsWidget->setObjectName("wifiDetailsWidget");
  connect(detailsWidget, &WifiDetails::connectToNetwork, this, &Networking::connectToNetwork);
  connect(detailsWidget, &WifiDetails::forgetNetwork, this, &Networking::forgetNetwork);
  connect(detailsWidget, &WifiDetails::backPress, [=]() { main_layout->setCurrentWidget(wifiScreen); });
  main_layout->addWidget(detailsWidget);

  an = new AdvancedNetworking(wifi, this);
  connect(an, &AdvancedNetworking::backPress, [=]() { main_layout->setCurrentWidget(wifiScreen); });
  main_layout->addWidget(an);

  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(0x29, 0x29, 0x29));
  setAutoFillBackground(true);
  setPalette(pal);

  setStyleSheet(R"(
    #wifiWidget > QPushButton, #back_btn, #advanced_btn {
      font-size: 50px;
      margin: 0px;
      padding: 15px;
      border-width: 0;
      border-radius: 30px;
      color: #dddddd;
      background-color: #393939;
    }
    #back_btn:pressed, #advanced_btn:pressed {
      background-color:  #4a4a4a;
    }
  )");
  main_layout->setCurrentWidget(wifiScreen);
}

void Networking::refresh() {
  wifiWidget->refresh();
  detailsWidget->refresh();
  an->refresh();
}

void Networking::connectToNetwork(const Network &n) {
  if (wifi->isKnownConnection(n.ssid)) {
    wifi->activateWifiConnection(n.ssid);
    wifiWidget->refresh();
  } else if (n.security_type == SecurityType::OPEN) {
    wifi->connect(n);
  } else if (n.security_type == SecurityType::WPA) {
    QString pass = InputDialog::getText(tr("Enter password"), this, tr("for \"%1\"").arg(QString::fromUtf8(n.ssid)), true, 8);
    if (!pass.isEmpty()) {
      wifi->connect(n, pass);
    }
  }
}

void Networking::viewNetwork(const Network &n) {
  detailsWidget->view(n);
  main_layout->setCurrentWidget(detailsWidget);
}

void Networking::forgetNetwork(const Network &n) {
  if (ConfirmationDialog::confirm(tr("Forget Wi-Fi Network \"%1\"?").arg(QString::fromUtf8(n.ssid)), this)) {
    wifi->forgetConnection(n.ssid);
    main_layout->setCurrentWidget(wifiScreen);
    refresh();
  }
}

void Networking::wrongPassword(const QString &ssid) {
  if (wifi->seenNetworks.contains(ssid)) {
    const Network &n = wifi->seenNetworks.value(ssid);
    QString pass = InputDialog::getText(tr("Wrong password"), this, tr("for \"%1\"").arg(QString::fromUtf8(n.ssid)), true, 8);
    if (!pass.isEmpty()) {
      wifi->connect(n, pass);
    }
  }
}

void Networking::showEvent(QShowEvent *event) {
  wifi->start();
}

void Networking::hideEvent(QHideEvent *event) {
  wifi->stop();
}

// AdvancedNetworking functions

AdvancedNetworking::AdvancedNetworking(WifiManager *wifi, QWidget *parent): QWidget(parent), wifi(wifi) {

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setMargin(40);
  main_layout->setSpacing(20);

  // Back button
  QPushButton* back = new QPushButton(tr("Back"));
  back->setObjectName("back_btn");
  back->setFixedSize(400, 100);
  connect(back, &QPushButton::clicked, [=]() { emit backPress(); });
  main_layout->addWidget(back, 0, Qt::AlignLeft);

  ListWidget *list = new ListWidget(this);
  // Enable tethering layout
  tetheringToggle = new ToggleControl(tr("Enable Tethering"), "", "", wifi->isTetheringEnabled());
  list->addItem(tetheringToggle);
  connect(tetheringToggle, &ToggleControl::toggleFlipped, this, &AdvancedNetworking::toggleTethering);

  // Change tethering password
  ButtonControl *editPasswordButton = new ButtonControl(tr("Tethering Password"), tr("EDIT"));
  connect(editPasswordButton, &ButtonControl::clicked, [=]() {
    QString pass = InputDialog::getText(tr("Enter new tethering password"), this, "", true, 8, wifi->getTetheringPassword());
    if (!pass.isEmpty()) {
      wifi->changeTetheringPassword(pass);
    }
  });
  list->addItem(editPasswordButton);

  // IP address
  ipLabel = new LabelControl(tr("IP Address"), wifi->ipv4_address);
  list->addItem(ipLabel);

  // SSH keys
  list->addItem(new SshToggle());
  list->addItem(new SshControl());

  // Roaming toggle
  const bool roamingEnabled = params.getBool("GsmRoaming");
  ToggleControl *roamingToggle = new ToggleControl(tr("Enable Roaming"), "", "", roamingEnabled);
  connect(roamingToggle, &SshToggle::toggleFlipped, [=](bool state) {
    params.putBool("GsmRoaming", state);
    wifi->updateGsmSettings(state, QString::fromStdString(params.get("GsmApn")));
  });
  list->addItem(roamingToggle);

  // APN settings
  ButtonControl *editApnButton = new ButtonControl(tr("APN Setting"), tr("EDIT"));
  connect(editApnButton, &ButtonControl::clicked, [=]() {
    const bool roamingEnabled = params.getBool("GsmRoaming");
    const QString cur_apn = QString::fromStdString(params.get("GsmApn"));
    QString apn = InputDialog::getText(tr("Enter APN"), this, tr("leave blank for automatic configuration"), false, -1, cur_apn).trimmed();

    if (apn.isEmpty()) {
      params.remove("GsmApn");
    } else {
      params.put("GsmApn", apn.toStdString());
    }
    wifi->updateGsmSettings(roamingEnabled, apn);
  });
  list->addItem(editApnButton);

  // Set initial config
  wifi->updateGsmSettings(roamingEnabled, QString::fromStdString(params.get("GsmApn")));

  main_layout->addWidget(new ScrollView(list, this));
  main_layout->addStretch(1);
}

void AdvancedNetworking::refresh() {
  ipLabel->setText(wifi->ipv4_address);
  tetheringToggle->setEnabled(true);
  update();
}

void AdvancedNetworking::toggleTethering(bool enabled) {
  wifi->setTetheringEnabled(enabled);
  tetheringToggle->setEnabled(false);
}

// WifiUI functions

WifiUI::WifiUI(WifiManager *wifi, QWidget *parent) : QWidget(parent), wifi(wifi) {
  main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  // load imgs
  for (const auto &s : {"low", "medium", "high", "full"}) {
    QPixmap pix(ASSET_PATH + "/offroad/icon_wifi_strength_" + s + ".svg");
    strengths.push_back(pix.scaledToHeight(68, Qt::SmoothTransformation));
  }
  lock = QPixmap(ASSET_PATH + "offroad/icon_lock_closed.svg").scaledToWidth(49, Qt::SmoothTransformation);
  checkmark = QPixmap(ASSET_PATH + "offroad/icon_checkmark.svg").scaledToWidth(49, Qt::SmoothTransformation);
  circled_slash = QPixmap(ASSET_PATH + "img_circled_slash.svg").scaledToWidth(49, Qt::SmoothTransformation);

  scanningLabel = new QLabel(tr("Scanning for networks..."));
  scanningLabel->setStyleSheet("font-size: 65px;");
  main_layout->addWidget(scanningLabel, 0, Qt::AlignCenter);

  list_layout = new QVBoxLayout;
  main_layout->addLayout(list_layout);

  setStyleSheet(R"(
    QScrollBar::handle:vertical {
      min-height: 0px;
      border-radius: 4px;
      background-color: #8A8A8A;
    }
    #editBtn {
      font-size: 32px;
      font-weight: 600;
      color: #292929;
      background-color: #BDBDBD;
      border-width: 1px solid #828282;
      border-radius: 5px;
      padding: 40px;
      padding-bottom: 16px;
      padding-top: 16px;
    }
    #connecting {
      font-size: 32px;
      font-weight: 600;
      color: white;
      border-radius: 0;
      padding: 27px;
      padding-left: 43px;
      padding-right: 43px;
      background-color: black;
    }
    #ssidLabel {
      font-size: 55px;
      font-weight: 300;
      text-align: left;
      border: none;
      padding-top: 50px;
      padding-bottom: 50px;
    }
    #ssidLabel[disconnected=false] {
      font-weight: 500;
    }
    #ssidLabel:disabled {
      color: #696969;
    }
  )");
}

void WifiUI::refresh() {
  // TODO: don't rebuild this every time
  clearLayout(list_layout);

  bool is_empty = wifi->seenNetworks.isEmpty();
  scanningLabel->setVisible(is_empty);
  if (is_empty) return;

  QList<Network> sortedNetworks = wifi->seenNetworks.values();
  std::sort(sortedNetworks.begin(), sortedNetworks.end(), compare_by_strength);

  // add networks
  ListWidget *list = new ListWidget(this);
  for (Network &network : sortedNetworks) {
    QHBoxLayout *hlayout = new QHBoxLayout;
    hlayout->setContentsMargins(44, 0, 73, 0);
    hlayout->setSpacing(50);

    // Clickable SSID label
    ElidedLabel *ssidLabel = new ElidedLabel(network.ssid);
    ssidLabel->setObjectName("ssidLabel");
    ssidLabel->setEnabled(network.security_type != SecurityType::UNSUPPORTED);
    ssidLabel->setProperty("disconnected", network.connected == ConnectedType::DISCONNECTED);
    if (network.connected == ConnectedType::DISCONNECTED) {
      QObject::connect(ssidLabel, &ElidedLabel::clicked, this, [=]() { emit connectToNetwork(network); });
    }
    hlayout->addWidget(ssidLabel, network.connected == ConnectedType::CONNECTING ? 0 : 1);

    if (network.connected == ConnectedType::CONNECTING) {
      QPushButton *connecting = new QPushButton(tr("CONNECTING..."));
      connecting->setObjectName("connecting");
      hlayout->addWidget(connecting, 2, Qt::AlignLeft);
    }

    // Edit button
    if (wifi->isKnownConnection(network.ssid)) {
      QPushButton *editBtn = new QPushButton(tr("EDIT"));
      editBtn->setObjectName("editBtn");
      QObject::connect(editBtn, &QPushButton::clicked, [=]() { emit viewNetwork(network); });
      hlayout->addWidget(editBtn, 0, Qt::AlignRight);
    }

    // Status icon
    if (network.connected == ConnectedType::CONNECTED) {
      QLabel *connectIcon = new QLabel();
      connectIcon->setPixmap(checkmark);
      hlayout->addWidget(connectIcon, 0, Qt::AlignRight);
    } else if (network.security_type == SecurityType::UNSUPPORTED) {
      QLabel *unsupportedIcon = new QLabel();
      unsupportedIcon->setPixmap(circled_slash);
      hlayout->addWidget(unsupportedIcon, 0, Qt::AlignRight);
    } else if (network.security_type == SecurityType::WPA) {
      QLabel *lockIcon = new QLabel();
      lockIcon->setPixmap(lock);
      hlayout->addWidget(lockIcon, 0, Qt::AlignRight);
    } else {
      hlayout->addSpacing(lock.width() + hlayout->spacing());
    }

    // Strength indicator
    QLabel *strength = new QLabel();
    strength->setPixmap(strengths[std::clamp((int)round(network.strength / 33.), 0, 3)]);
    hlayout->addWidget(strength, 0, Qt::AlignRight);

    list->addItem(hlayout);
  }
  list_layout->addWidget(list);
  list_layout->addStretch(1);
}

WifiDetails::WifiDetails(WifiManager *wifi, QWidget *parent) : QWidget(parent), wifi(wifi) {
  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setMargin(40);
  main_layout->setSpacing(20);

  // Back button
  auto back_btn = new QPushButton(tr("Back"));
  back_btn->setObjectName("back_btn");
  back_btn->setFixedSize(400, 100);
  QObject::connect(back_btn, &QPushButton::clicked, [=]() { emit backPress(); });
  main_layout->addWidget(back_btn, 0, Qt::AlignLeft);

  // Header
  // - SSID name
  // - Connection state (connected/disconnected)
  {
    auto ssid_layout = new QVBoxLayout();

    auto ssid = QString::fromUtf8(network.ssid);
    auto ssid_label = new QLabel(ssid);
    ssid_label->setObjectName("ssid_label");
    ssid_layout->addWidget(ssid_label);

    auto state_label = new QLabel(tr("Connected"));
    state_label->setObjectName("state_label");
    ssid_layout->addWidget(state_label);

    main_layout->addLayout(ssid_layout);
  }

  // Controls
  // - Connect
  // - Forget
  auto controls_layout = new QHBoxLayout();

  // TODO: icon button
  connect_btn = new QPushButton(tr("Connect"));
  connect_btn->setProperty("class", "control");
  connect_btn->setFixedSize(300, 100);
  connect(connect_btn, &QPushButton::clicked, this, [=]() {
    if (network.connected == ConnectedType::DISCONNECTED) {
      emit connectToNetwork(network);
      emit backPress();
    }
  });
  controls_layout->addWidget(connect_btn);

  // TODO: icon button
  forget_btn = new QPushButton(tr("Forget"));
  forget_btn->setProperty("class", "control");
  forget_btn->setFixedSize(300, 100);
  connect(forget_btn, &QPushButton::clicked, this, [=]() {
    if (wifi->isKnownConnection(network.ssid)) {
      emit forgetNetwork(network);
      emit backPress();
    }
  });
  controls_layout->addWidget(forget_btn);

  main_layout->addLayout(controls_layout);

  // Network details
  // - Signal strength
  // - Frequency (TODO)
  // - Security
  // - Metered (detect automatically/metered/not metered) (TODO)
  ListWidget* list = new ListWidget(this);

  signal_label = new LabelControl(tr("Signal Strength"), "");
  list->addItem(signal_label);

  security_label = new LabelControl(tr("Security"), "");
  list->addItem(security_label);

  // TODO: fix not full width
  main_layout->addWidget(new ScrollView(list, this));
  main_layout->addStretch(1);

  refresh();

  setStyleSheet(R"(
    QPushButton.control {
      font-size: 32px;
      font-weight: 600;
      color: #292929;
      background-color: #BDBDBD;
      border-width: 1px solid #828282;
      border-radius: 5px;
      padding: 40px;
      padding-bottom: 16px;
      padding-top: 16px;
    }
    QPushButton.control:disabled {
      background-color: #909090;
    }
    #connecting {
      font-size: 32px;
      font-weight: 600;
      color: white;
      border-radius: 0;
      padding: 27px;
      padding-left: 43px;
      padding-right: 43px;
      background-color: black;
    }
    #ssidLabel {
      font-size: 55px;
      font-weight: 300;
      text-align: left;
      border: none;
      padding-top: 50px;
      padding-bottom: 50px;
    }
    #ssidLabel[disconnected=false] {
      font-weight: 500;
    }
    #ssidLabel:disabled {
      color: #696969;
    }
  )");
}

void WifiDetails::view(const Network &n) {
  network = n;
  refresh();
}

void WifiDetails::refresh() {
  // SSID
  QString ssid = QString::fromUtf8(network.ssid);
  ssid_label->setText(ssid);

  // State
  QString state;
  switch (network.connected) {
    case ConnectedType::DISCONNECTED:
      state = tr("Disconnected");
      break;
    case ConnectedType::CONNECTING:
      state = tr("Connecting");
      break;
    case ConnectedType::CONNECTED:
      state = tr("Connected");
      break;
  }
  state_label->setText(state);

  // Controls
  connect_btn->setEnabled(network.connected == ConnectedType::DISCONNECTED);
  auto known_connection = wifi->isKnownConnection(network.ssid);
  forget_btn->setDisabled(!known_connection);

  // Signal strength (None, Weak, OK, Excellent)
  int strength = std::clamp((int)round(network.strength / 33.), 0, 3);
  QString signal;
  switch (strength) {
    case 0:
      signal = tr("None");
      break;
    case 1:
      signal = tr("Weak");
      break;
    case 2:
      signal = tr("OK");
      break;
    case 3:
      signal = tr("Excellent");
      break;
  }
  signal_label->setValue(signal);

  // Security
  QString security;
  switch (network.security_type) {
    case SecurityType::OPEN:
      security = tr("Open");
      break;
    case SecurityType::WPA:
      security = tr("WPA2");
      break;
    case SecurityType::UNSUPPORTED:
      security = tr("Unsupported");
      break;
  }
  security_label->setValue(security);

  update();
}
