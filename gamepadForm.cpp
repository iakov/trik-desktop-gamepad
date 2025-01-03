/* Copyright 2015-2016 CyberTech Labs Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file was modified by Yurii Litvinov, Aleksei Alekseev, Mikhail Wall and Konstantin Batoev
 * to make it comply with the requirements of trikRuntime project.
 * See git revision history for detailed changes. */

#include "gamepadForm.h"
#include "ui_gamepadForm.h"

#include <QtWidgets/QMessageBox>
#include <QtGui/QKeyEvent>

#include <QtNetwork/QNetworkRequest>
#include <QtGui/QFontDatabase>

#ifdef TRIK_USE_QT6
#else
	#include <QtMultimedia/QMediaContent>
#endif

GamepadForm::GamepadForm()
	: mUi(new Ui::GamepadForm())
	, strategy(Strategy::getStrategy(Strategies::standartStrategy,this))
	, mSettings(QSettings::Format::NativeFormat, QSettings::Scope::UserScope, "CyberTech Labs", "desktop-gamepad")
{
	mUi->setupUi(this);
	this->installEventFilter(this);
	connectionManager = new ConnectionManager(&mSettings);
	/// passing this to QTcpSocket allows `socket` to be moved
	/// to another thread with the parent
	/// when connectionManager.moveToThread() is called
	qRegisterMetaType<QAbstractSocket::SocketState>();
	connectionManager->moveToThread(&thread);
	connect(this, &GamepadForm::newConnectionParameters, this, &GamepadForm::restartVideoStream);
	connect(this, &GamepadForm::newConnectionParameters, connectionManager, &ConnectionManager::reconnectToHost);
	connect(&thread, &QThread::started, connectionManager, &ConnectionManager::init);
	connect(&thread, &QThread::finished, connectionManager, &ConnectionManager::deleteLater);
	setUpGamepadForm();
	thread.start();
}

GamepadForm::~GamepadForm()
{
	thread.quit();
	thread.wait();

	delete player;
	delete mUi;
}

void GamepadForm::startControllerFromSysArgs(const QStringList &args)
{
	const auto &gamepadIp = args.at(1);
	const auto &gamepadPort = args.size() < 3 ? "4444" : args.at(2);
	const auto &cameraPort = args.size() < 4 ? "8080" : args.at(3);
	const auto &cameraIp = args.size() < 5 ? gamepadIp : args.at(4);
	mSettings.setValue("cameraIp", cameraIp);
	mSettings.setValue("cameraPort", cameraPort);
	mSettings.setValue("gamepadIp", gamepadIp);
	mSettings.setValue("gamepadPort", gamepadPort);
	Q_EMIT newConnectionParameters();
}

void GamepadForm::setUpGamepadForm()
{
	createMenu();
	setFontToPadButtons();
	setUpControlButtonsHash();
	createConnection();
	setVideoController();
	setLabels();
	setImageControl();
	retranslate();
}

void GamepadForm::setVideoController()
{
	videoWidget = new QVideoWidget(this);
	videoWidget->setMinimumSize(320, 240);
	videoWidget->setVisible(false);
	mUi->verticalLayout->addWidget(videoWidget);
	mUi->verticalLayout->setAlignment(videoWidget, Qt::AlignCenter);

#ifdef TRIK_USE_QT6
	player = new QMediaPlayer(videoWidget);
#else
	player = new QMediaPlayer(videoWidget, QMediaPlayer::StreamPlayback);
#endif

	connect(player, &QMediaPlayer::mediaStatusChanged, this, &GamepadForm::handleMediaStatusChanged);
#ifdef TRIK_USE_QT6
	connect(player, &QMediaPlayer::errorOccurred, this, &GamepadForm::handleMediaPlayerError);
#else
	connect(player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error)
			, this, &GamepadForm::handleMediaPlayerError);
#endif

	player->setVideoOutput(videoWidget);

	movie.setFileName(":/images/loading.gif");
	mUi->loadingMediaLabel->setVisible(false);
	mUi->loadingMediaLabel->setMovie(&movie);

	QPixmap pixmap(":/images/noVideoSign.png");
	mUi->invalidMediaLabel->setPixmap(pixmap);

	mUi->loadingMediaLabel->setVisible(false);
	mUi->invalidMediaLabel->setVisible(false);

}

void GamepadForm::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
	mTakeImageAction->setEnabled(false);
	movie.setPaused(true);
	switch (status) {
	case QMediaPlayer::StalledMedia:
	case QMediaPlayer::LoadedMedia:
	case QMediaPlayer::BufferingMedia:
		player->play();
		mTakeImageAction->setEnabled(true);
		mUi->loadingMediaLabel->setVisible(false);
		mUi->invalidMediaLabel->setVisible(false);
		mUi->label->setVisible(false);
		videoWidget->setVisible(true);
		break;

	case QMediaPlayer::LoadingMedia:
		mUi->invalidMediaLabel->setVisible(false);
		mUi->label->setVisible(false);
		videoWidget->setVisible(false);
		mUi->loadingMediaLabel->setVisible(true);
		movie.setPaused(false);
		break;

	case QMediaPlayer::InvalidMedia:
		mUi->loadingMediaLabel->setVisible(false);
		mUi->label->setVisible(false);
		videoWidget->setVisible(false);
		mUi->invalidMediaLabel->setVisible(true);
		break;

	case QMediaPlayer::NoMedia:
	case QMediaPlayer::EndOfMedia:
		mUi->loadingMediaLabel->setVisible(false);
		mUi->invalidMediaLabel->setVisible(false);
		videoWidget->setVisible(false);
		mUi->label->setVisible(true);
		break;

	default:
		break;
	}
}

void GamepadForm::handleMediaPlayerError(QMediaPlayer::Error error)
{
	qDebug() << "ERROR:" << error << player->errorString();
}

void GamepadForm::restartVideoStream()
{
	const auto &cIp = mSettings.value("cameraIp").toString();
	const auto &cPort = mSettings.value("cameraPort").toString();
	const auto status = player->mediaStatus();
	if (status == QMediaPlayer::NoMedia || status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
		const QString url = "http://" + cIp + ":" + cPort + "/?action=stream&filename=noname.jpg";
		// QNetworkRequest nr = QNetworkRequest(url);
		// nr.setPriority(QNetworkRequest::LowPriority);
		// nr.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysCache);
#ifdef TRIK_USE_QT6
		player->setSource(QUrl(url));
#else
		player->setMedia(QUrl(url));
#endif
	}
}

void GamepadForm::checkSocket(QAbstractSocket::SocketState state)
{
	switch (state) {
	case QAbstractSocket::ConnectedState:
		mUi->disconnectedLabel->setVisible(false);
		mUi->connectedLabel->setVisible(true);
		mUi->connectingLabel->setVisible(false);
		setButtonsCheckable(true);
		setButtonsEnabled(true);
		break;

	case QAbstractSocket::ConnectingState:
		mUi->disconnectedLabel->setVisible(false);
		mUi->connectedLabel->setVisible(false);
		mUi->connectingLabel->setVisible(true);
		setButtonsCheckable(false);
		setButtonsEnabled(false);
		break;

	case QAbstractSocket::UnconnectedState:
	default:
		mUi->disconnectedLabel->setVisible(true);
		mUi->connectedLabel->setVisible(false);
		mUi->connectingLabel->setVisible(false);
		setButtonsCheckable(false);
		setButtonsEnabled(false);
		break;
	}
}

void GamepadForm::checkBytesWritten(int result)
{
	if (result == -1) {
		setButtonsEnabled(false);
		setButtonsCheckable(false);
	}
}

void GamepadForm::showConnectionFailedMessage()
{
	QMessageBox failedConnectionMessage(this);
	failedConnectionMessage.setText(tr("Couldn't connect to robot"));
	failedConnectionMessage.exec();
}

void GamepadForm::setFontToPadButtons()
{
	const int id = QFontDatabase::addApplicationFont(":/fonts/freemono.ttf");
	const QString family = QFontDatabase::applicationFontFamilies(id).at(0);
	const int pointSize = 50;
	QFont font(family, pointSize);

	mUi->buttonPad1Down->setFont(font);
	mUi->buttonPad1Up->setFont(font);
	mUi->buttonPad1Left->setFont(font);
	mUi->buttonPad1Right->setFont(font);

	mUi->buttonPad2Down->setFont(font);
	mUi->buttonPad2Up->setFont(font);
	mUi->buttonPad2Left->setFont(font);
	mUi->buttonPad2Right->setFont(font);
}

void GamepadForm::setButtonChecked(const int &key, bool checkStatus)
{
	controlButtonsHash[key]->setChecked(checkStatus);
}

void GamepadForm::createConnection()
{
	for (auto &&button : controlButtonsHash) {
		connect(button, &QPushButton::pressed, this, [this, button](){handleButtonPress(button); });
		connect(button, &QPushButton::released, this, [this, button](){handleButtonRelease(button); });
	}

	connect(connectionManager, &ConnectionManager::stateChanged, this, &GamepadForm::checkSocket);
	connect(connectionManager, &ConnectionManager::dataWasWritten, this, &GamepadForm::checkBytesWritten);
	connect(connectionManager, &ConnectionManager::connectionFailed, this, &GamepadForm::showConnectionFailedMessage);
	connect(this, &GamepadForm::commandReceived, connectionManager, &ConnectionManager::write);

	connect(strategy, &Strategy::commandPrepared, this, &GamepadForm::sendCommand);
	connect(qApp, &QApplication::applicationStateChanged, this, &GamepadForm::dealWithApplicationState);
}

void GamepadForm::createMenu()
{
	// Path to your local directory. Set up if you don't put .qm files into your debug folder.
	const auto &pathToDir = mSettings.value("languagesPath", ":i18n/trikDesktopGamepad").toString();

	const auto &russian = pathToDir + "_ru";
	const auto &english = pathToDir + "_en";
	const auto &french = pathToDir + "_fr";
	const auto &german = pathToDir + "_de";

	mMenuBar = new QMenuBar(this);

	mConnectionMenu = new QMenu(this);
	mMenuBar->addMenu(mConnectionMenu);

	mModeMenu = new QMenu(this);
	mMenuBar->addMenu(mModeMenu);

	mImageMenu = new QMenu(this);
	mMenuBar->addMenu(mImageMenu);
	mTakeImageAction = new QAction(this);
	mImageMenu->addAction(mTakeImageAction);
	mTakeImageAction->setEnabled(false);
	mTakeImageAction->setShortcut(QKeySequence("Ctrl+I"));
	connect(mTakeImageAction, &QAction::triggered, this, &GamepadForm::requestImage);

	mLanguageMenu = new QMenu(this);
	mMenuBar->addMenu(mLanguageMenu);

	mConnectAction = new QAction(this);
	// Set to QKeySequence for Ctrl+N shortcut
	mConnectAction->setShortcuts(QKeySequence::New);
	connect(mConnectAction, &QAction::triggered, this, &GamepadForm::openConnectDialog);

	mExitAction = new QAction(this);
	mExitAction->setShortcuts(QKeySequence::Quit);
	connect(mExitAction, &QAction::triggered, this, &GamepadForm::exit);

	mModesActions = new QActionGroup(this);
	mStandartStrategyAction = new QAction(this);
	mAccelerateStrategyAction = new QAction(this);
	mStandartStrategyAction->setCheckable(true);
	mAccelerateStrategyAction->setCheckable(true);
	mStandartStrategyAction->setChecked(true);
	connect(mStandartStrategyAction, &QAction::triggered, this, [this](){changeMode(Strategies::standartStrategy);});
	connect(mAccelerateStrategyAction, &QAction::triggered,
		this, [this](){changeMode(Strategies::accelerateStrategy);});
	mModesActions->addAction(mStandartStrategyAction);
	mModesActions->addAction(mAccelerateStrategyAction);
	mModesActions->setExclusive(true);

	mRussianLanguageAction = new QAction(this);
	mEnglishLanguageAction = new QAction(this);
	mFrenchLanguageAction = new QAction(this);
	mGermanLanguageAction = new QAction(this);

	mLanguages = new QActionGroup(this);
	mLanguages->addAction(mRussianLanguageAction);
	mLanguages->addAction(mEnglishLanguageAction);
	mLanguages->addAction(mFrenchLanguageAction);
	mLanguages->addAction(mGermanLanguageAction);
	mLanguages->setExclusive(true);

	// Set up language actions checkable
	mEnglishLanguageAction->setCheckable(true);
	mRussianLanguageAction->setCheckable(true);
	mFrenchLanguageAction->setCheckable(true);
	mGermanLanguageAction->setCheckable(true);
	mEnglishLanguageAction->setChecked(true);

	// Connecting languages to menu items
	connect(mRussianLanguageAction, &QAction::triggered, this, [this, russian](){changeLanguage(russian);});
	connect(mEnglishLanguageAction, &QAction::triggered, this, [this, english](){changeLanguage(english);});
	connect(mFrenchLanguageAction, &QAction::triggered, this, [this, french](){changeLanguage(french);});
	connect(mGermanLanguageAction, &QAction::triggered, this, [this, german](){changeLanguage(german);});

	mAboutAction = new QAction(this);
	connect(mAboutAction, &QAction::triggered, this, &GamepadForm::about);

	mConnectionMenu->addAction(mConnectAction);
	mConnectionMenu->addAction(mExitAction);

	mModeMenu->addAction(mStandartStrategyAction);
	mModeMenu->addAction(mAccelerateStrategyAction);

	mLanguageMenu->addAction(mRussianLanguageAction);
	mLanguageMenu->addAction(mEnglishLanguageAction);
	mLanguageMenu->addAction(mFrenchLanguageAction);
	mLanguageMenu->addAction(mGermanLanguageAction);

	mMenuBar->addAction(mAboutAction);

	this->layout()->setMenuBar(mMenuBar);
}

void GamepadForm::setButtonsEnabled(bool enabled)
{
	// Here we enable or disable pads and "magic buttons" depending on given parameter.
	for (auto &&button : controlButtonsHash.values())
		button->setEnabled(enabled);
}

void GamepadForm::setButtonsCheckable(bool checkableStatus)
{
	for (auto &&button : controlButtonsHash.values())
		button->setCheckable(checkableStatus);
}

void GamepadForm::setUpControlButtonsHash()
{
	controlButtonsHash.insert(Qt::Key_1, mUi->button1);
	controlButtonsHash.insert(Qt::Key_2, mUi->button2);
	controlButtonsHash.insert(Qt::Key_3, mUi->button3);
	controlButtonsHash.insert(Qt::Key_4, mUi->button4);
	controlButtonsHash.insert(Qt::Key_5, mUi->button5);

	controlButtonsHash.insert(Qt::Key_A, mUi->buttonPad1Left);
	controlButtonsHash.insert(Qt::Key_D, mUi->buttonPad1Right);
	controlButtonsHash.insert(Qt::Key_W, mUi->buttonPad1Up);
	controlButtonsHash.insert(Qt::Key_S, mUi->buttonPad1Down);

	controlButtonsHash.insert(Qt::Key_Left, mUi->buttonPad2Left);
	controlButtonsHash.insert(Qt::Key_Right, mUi->buttonPad2Right);
	controlButtonsHash.insert(Qt::Key_Up, mUi->buttonPad2Up);
	controlButtonsHash.insert(Qt::Key_Down, mUi->buttonPad2Down);
}

void GamepadForm::setLabels()
{
	QPixmap redBall(":/images/redBall.png");
	mUi->disconnectedLabel->setPixmap(redBall);
	mUi->disconnectedLabel->setVisible(true);

	QPixmap greenBall(":/images/greenBall.png");
	mUi->connectedLabel->setPixmap(greenBall);
	mUi->connectedLabel->setVisible(false);

	QPixmap blueBall(":/images/blueBall.png");
	mUi->connectingLabel->setPixmap(blueBall);
	mUi->connectingLabel->setVisible(false);
}

void GamepadForm::setImageControl()
{
#ifdef TRIK_USE_QT6
	sink = videoWidget->videoSink();
	connect(sink, &QVideoSink::videoFrameChanged, this, &GamepadForm::saveImageToClipboard, Qt::QueuedConnection);
	player->setVideoSink(sink);
#else
	probe = new QVideoProbe(this);
	connect(probe, &QVideoProbe::videoFrameProbed, this, &GamepadForm::saveImageToClipboard, Qt::QueuedConnection);
	probe->setSource(player);
#endif
	isFrameNecessary = false;
	clipboard = QApplication::clipboard();
}

bool GamepadForm::eventFilter(QObject *obj, QEvent *event)
{
	Q_UNUSED(obj)

	// Handle key press event for View
	if(event->type() == QEvent::KeyPress) {
		int pressedKey = (dynamic_cast<QKeyEvent *> (event))->key();
		if (controlButtonsHash.keys().contains(pressedKey))
			setButtonChecked(pressedKey, true);

	} else if(event->type() == QEvent::KeyRelease) {

		QKeyEvent *keyEvent = dynamic_cast<QKeyEvent*>(event);

		auto releasedKey = keyEvent->key();
		if (controlButtonsHash.keys().contains(releasedKey))
			setButtonChecked(releasedKey, false);
	}

	// delegating events to Command-generating-strategy
	strategy->processEvent(event);

	return false;
}

void GamepadForm::sendCommand(const QString &command)
{
	if (!connectionManager->isConnected()) {
		return;
	}

	Q_EMIT commandReceived(command);
}

void GamepadForm::changeMode(Strategies type)
{
	auto oldStratedy = strategy;
	strategy = Strategy::getStrategy(type, this);
	connect(strategy, &Strategy::commandPrepared, this, &GamepadForm::sendCommand);
	delete oldStratedy;
}

void GamepadForm::dealWithApplicationState(Qt::ApplicationState state)
{
	if (state != Qt::ApplicationActive) {
		strategy->reset();
		for (auto &&button : controlButtonsHash)
			button->setChecked(false);
	}
}

void GamepadForm::saveImageToClipboard(QVideoFrame buffer)
{
	if (isFrameNecessary) {
		isFrameNecessary = false;
		QVideoFrame frame(buffer);
#ifdef TRIK_USE_QT6
		frame.map(QVideoFrame::ReadOnly);
		QImage::Format imageFormat = QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat());
#else
		frame.map(QAbstractVideoBuffer::ReadOnly);
		QImage::Format imageFormat = QVideoFrame::imageFormatFromPixelFormat(frame.pixelFormat());
#endif
		QImage img;
		// check whether videoframe can be transformed to qimage by qt
		if (imageFormat != QImage::Format_Invalid) {
#ifdef TRIK_USE_QT6
			img = frame.toImage();
#else
			img = QImage(frame.bits(),
						 frame.width(),
						 frame.height(),
						 // frame.bytesPerLine(),
						 imageFormat);
#endif
		} else {
			int width = frame.width();
			int height = frame.height();
			int size = height * width;
#ifdef TRIK_USE_QT6
			const uchar *data = frame.bits(0);
#else
			const uchar *data = frame.bits();
#endif

			img = QImage(width, height, QImage::Format_RGB32);
			/// converting from yuv420 to rgb32
			for (int i = 0; i < height; i++)
				for (int j = 0; j < width; j++) {
					int y = static_cast<int> (data[i * width + j]);
					int u = static_cast<int> (data[(i / 2) * (width / 2) + (j / 2) + size]);
					int v = static_cast<int> (data[(i / 2) * (width / 2) + (j / 2) + size + (size / 4)]);

					int r = y + int(1.13983 * (v - 128));
					int g = y - int(0.39465 * (u - 128)) - int(0.58060 * (v - 128));
					int b = y + int(2.03211 * (u - 128));

					r = qBound(0, r, 255);
					g = qBound(0, g, 255);
					b = qBound(0, b, 255);

					img.setPixel(j, i, qRgb(r, g, b));
				}
		}

		clipboard->setImage(img);
		frame.unmap();
	}
}

void GamepadForm::requestImage()
{
	isFrameNecessary = true;
}

void GamepadForm::openConnectDialog()
{
	mMyNewConnectForm = new ConnectForm(connectionManager, &mSettings, this);
	connect(mMyNewConnectForm, &ConnectForm::newConnectionParameters, this, &GamepadForm::newConnectionParameters);
	mMyNewConnectForm->show();
}

void GamepadForm::exit()
{
	qApp->exit();
}

void GamepadForm::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange)
	{
		mUi->retranslateUi(this);

		retranslate();
	}

	QWidget::changeEvent(event);
}

void GamepadForm::handleButtonPress(QWidget *widget)
{
	QPushButton *padButton = dynamic_cast<QPushButton *> (widget);
	padButton->setChecked(true);
	auto key = controlButtonsHash.key(padButton);
	QKeyEvent keyEvent(QEvent::KeyPress, key, Qt::NoModifier);
	strategy->processEvent(&keyEvent);
}

void GamepadForm::handleButtonRelease(QWidget *widget)
{
	QPushButton *padButton = dynamic_cast<QPushButton *> (widget);
	padButton->setChecked(false);
	auto key = controlButtonsHash.key(padButton);
	QKeyEvent keyEvent(QEvent::KeyRelease, key, Qt::NoModifier);
	strategy->processEvent(&keyEvent);
}

void GamepadForm::retranslate()
{
	mConnectionMenu->setTitle(tr("&Connection"));
	mModeMenu->setTitle(tr("&Mode"));
	mLanguageMenu->setTitle(tr("&Language"));

	mConnectAction->setText(tr("&Connect"));
	mExitAction->setText(tr("&Exit"));

	mStandartStrategyAction->setText(tr("&Simple"));
	mAccelerateStrategyAction->setText(tr("&Accelerate"));

	mRussianLanguageAction->setText(tr("&Russian"));
	mEnglishLanguageAction->setText(tr("&English"));
	mFrenchLanguageAction->setText(tr("&French"));
	mGermanLanguageAction->setText(tr("&German"));

	mImageMenu->setTitle(tr("&Image"));
	mTakeImageAction->setText(tr("&Screenshot to clipboard"));

	mAboutAction->setText(tr("&About"));

}

void GamepadForm::changeLanguage(const QString &language)
{
	mTranslator = new QTranslator(this);
	if (!mTranslator->load(language)) {
		qDebug() << "Failed to load translations for" << language;
	}
	qApp->installTranslator(mTranslator);
	mUi->retranslateUi(this);
}

void GamepadForm::about()
{
	const QString title = tr("About TRIK Gamepad");
	const QString about =  "TRIK Gamepad 2.1.0\n\n"+tr("This is desktop gamepad for TRIK robots.");
	QMessageBox::about(this, title, about);
}
