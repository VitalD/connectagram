/***********************************************************************
 *
 * Copyright (C) 2009, 2012 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "window.h"

#include "board.h"
#include "clock.h"
#include "definitions.h"
#include "locale_dialog.h"
#include "new_game_dialog.h"
#include "pattern.h"
#include "score_board.h"
#include "view.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>

Window::Window() {
	setWindowTitle(QCoreApplication::applicationName());
	setWindowIcon(QIcon(":/connectagram.png"));

	m_board = new Board(this);
	connect(m_board, SIGNAL(finished()), this, SLOT(gameFinished()));
	connect(m_board, SIGNAL(started()), this, SLOT(gameStarted()));
	connect(m_board, SIGNAL(pauseChanged()), this, SLOT(gamePauseChanged()));

	QWidget* contents = new QWidget(this);
	setCentralWidget(contents);

	View* view = new View(m_board, contents);

	m_scores = new ScoreBoard(this);

	m_definitions = new Definitions(this);
	connect(m_board, SIGNAL(wordAdded(const QString&)), m_definitions, SLOT(addWord(const QString&)));
	connect(m_board, SIGNAL(wordSolved(const QString&, const QString&)), m_definitions, SLOT(solveWord(const QString&, const QString&)));
	connect(m_board, SIGNAL(wordSelected(const QString&)), m_definitions, SLOT(selectWord(const QString&)));
	connect(m_board, SIGNAL(loading()), m_definitions, SLOT(clear()));

	// Create success message
	m_success = new QLabel(contents);
	m_success->setAttribute(Qt::WA_TransparentForMouseEvents);

	QFont f = font();
	f.setPointSize(24);
	QFontMetrics metrics(f);
	int width = metrics.width(tr("Success"));
	int height = metrics.height();
	QPixmap pixmap(QSize(width + height, height * 2));
	pixmap.fill(QColor(0, 0, 0, 0));
	{
		QPainter painter(&pixmap);

		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, 200));
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.drawRoundedRect(0, 0, width + height, height * 2, 10, 10);

		painter.setFont(f);
		painter.setPen(Qt::white);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		painter.drawText(height / 2, height / 2 + metrics.ascent(), tr("Success"));
	}
	m_success->setPixmap(pixmap);
	m_success->hide();
	connect(m_board, SIGNAL(loading()), m_success, SLOT(hide()));

	// Create overlay background
	QLabel* overlay = new QLabel(this);

	f = font();
	f.setPixelSize(20);
	metrics = QFontMetrics(f);
	width = qMax(metrics.width(tr("Loading")), metrics.width(tr("Paused")));
	for (int i = 0; i < 10; ++i) {
		QString test(6, QChar(i + 48));
		test.insert(4, QLatin1Char(':'));
		test.insert(2, QLatin1Char(':'));
		width = qMax(width, metrics.width(test));
	}
	pixmap = QPixmap(QSize(width + 82, 32));
	pixmap.fill(Qt::transparent);
	{
		QPainter painter(&pixmap);

		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, 200));
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.drawRoundedRect(0, -32, width + 82, 64, 5, 5);
	}
	overlay->setPixmap(pixmap);

	// Create overlay buttons
	m_definitions_button = new QLabel(overlay);
	m_definitions_button->setPixmap(QString(":/definitions.png"));
	m_definitions_button->setCursor(Qt::PointingHandCursor);
	m_definitions_button->setToolTip(tr("Definitions"));
	m_definitions_button->installEventFilter(this);

	m_hint_button = new QLabel(overlay);
	m_hint_button->setPixmap(QString(":/hint.png"));
	m_hint_button->setCursor(Qt::PointingHandCursor);
	m_hint_button->setToolTip(tr("Hint"));
	m_hint_button->setDisabled(true);
	m_hint_button->installEventFilter(this);
	connect(m_board, SIGNAL(hintAvailable(bool)), m_hint_button, SLOT(setEnabled(bool)));

	// Create clock
	m_clock = new Clock(overlay);
	m_clock->setDisabled(true);
	connect(m_clock, SIGNAL(togglePaused()), m_board, SLOT(togglePaused()));
	connect(m_board, SIGNAL(loading()), m_clock, SLOT(setLoading()));

	QHBoxLayout* overlay_layout = new QHBoxLayout(overlay);
	overlay_layout->setMargin(0);
	overlay_layout->setSpacing(0);
	overlay_layout->addSpacing(10);
	overlay_layout->addWidget(m_definitions_button);
	overlay_layout->addStretch();
	overlay_layout->addWidget(m_clock, 0, Qt::AlignCenter);
	overlay_layout->addStretch();
	overlay_layout->addWidget(m_hint_button);
	overlay_layout->addSpacing(10);

	// Lay out board
	QGridLayout* layout = new QGridLayout(contents);
	layout->setMargin(0);
	layout->setSpacing(0);
	layout->addWidget(view, 0, 0);
	layout->addWidget(m_success, 0, 0, Qt::AlignCenter);
	layout->addWidget(overlay, 0, 0, Qt::AlignHCenter | Qt::AlignTop);

	// Create menus
	QMenu* menu = menuBar()->addMenu(tr("&Game"));
	menu->addAction(tr("&New"), this, SLOT(newGame()), tr("Ctrl+N"));
	menu->addAction(tr("&Details"), this, SLOT(showDetails()));
	menu->addSeparator();
	m_pause_action = menu->addAction(tr("&Pause"), m_board, SLOT(togglePaused()), tr("P"));
	m_pause_action->setDisabled(true);
	QAction* hint_action = menu->addAction(tr("&Hint"), m_board, SLOT(showHint()), tr("H"));
	hint_action->setDisabled(true);
	connect(m_board, SIGNAL(hintAvailable(bool)), hint_action, SLOT(setEnabled(bool)));
	menu->addAction(tr("D&efinitions"), m_definitions, SLOT(selectWord()), tr("D"));
	menu->addSeparator();
	menu->addAction(tr("&Scores"), m_scores, SLOT(exec()));
	menu->addSeparator();
	menu->addAction(tr("&Quit"), qApp, SLOT(quit()), tr("Ctrl+Q"));

	menu = menuBar()->addMenu(tr("&Settings"));
	menu->addAction(tr("Application &Language..."), this, SLOT(setLocale()));

	menu = menuBar()->addMenu(tr("&Help"));
	menu->addAction(tr("&About"), this, SLOT(about()));
	menu->addAction(tr("About &Qt"), qApp, SLOT(aboutQt()));

	// Restore window geometry
	QSettings settings;
	resize(800, 600);
	restoreGeometry(settings.value("Geometry").toByteArray());

	// Continue previous or start new game
	show();
	if (settings.contains("Current/Words")) {
		m_board->openGame();
	} else {
		settings.remove("Current");
		newGame();
	}
}

//-----------------------------------------------------------------------------

void Window::newGame() {
	NewGameDialog dialog(m_board, this);
	dialog.exec();
}

//-----------------------------------------------------------------------------

void Window::closeEvent(QCloseEvent* event) {
	m_board->saveGame();
	QSettings().setValue("Geometry", saveGeometry());
	QMainWindow::closeEvent(event);
}

//-----------------------------------------------------------------------------

bool Window::eventFilter(QObject* object, QEvent* event) {
	if (event->type() == QEvent::MouseButtonPress) {
		if (object == m_definitions_button) {
			m_definitions->selectWord();
			return true;
		} else if (object == m_hint_button) {
			m_board->showHint();
			return true;
		}
	}
	return QMainWindow::eventFilter(object, event);
}

//-----------------------------------------------------------------------------

bool Window::event(QEvent* event) {
	if (event->type() == QEvent::WindowBlocked || event->type() == QEvent::WindowDeactivate) {
		m_board->setPaused(true);
	}
	return QMainWindow::event(event);
}

//-----------------------------------------------------------------------------

void Window::about() {
	QMessageBox::about(this, tr("About"), tr(
		"<p><center><big><b>Connectagram %1</b></big><br/>"
		"A word unscrambling game<br/>"
		"<small>Copyright &copy; 2009-%2 by Graeme Gott</small><br/>"
		"<small>Released under the <a href=\"http://www.gnu.org/licenses/gpl.html\">GPL 3</a> license</small></center></p>"
		"<p><center>Definitions are from <a href=\"http://www.dict.org/\">dict.org</a></center></p>"
	).arg(QCoreApplication::applicationVersion()).arg("2012"));
}

//-----------------------------------------------------------------------------

void Window::showDetails() {
	Pattern* pattern = m_board->pattern();
	if (pattern) {
		QMessageBox::information(this, tr("Details"), tr(
			"<p><b>Pattern:</b> %1<br>"
			"<b>Characters per word:</b> %2<br>"
			"<b>Number of words:</b> %3<br>"
			"<b>Game number:</b> %L4</p>"
		).arg(pattern->name()).arg(pattern->wordLength() + 1).arg(pattern->wordCount()).arg(pattern->seed()));
	}
}

//-----------------------------------------------------------------------------

void Window::setLocale() {
	LocaleDialog dialog(this);
	dialog.exec();
}

//-----------------------------------------------------------------------------

void Window::gameStarted() {
	m_clock->setEnabled(true);
	m_clock->start();
	m_pause_action->setEnabled(true);
	m_success->hide();
}

//-----------------------------------------------------------------------------

void Window::gameFinished() {
	QSettings settings;
	int count = settings.value("Current/Count").toInt();
	int length = settings.value("Current/Length").toInt();
	int msecs = settings.value("Current/Time").toInt();

	m_clock->stop();
	m_clock->setDisabled(true);
	m_pause_action->setDisabled(true);
	m_success->show();

	m_scores->addScore(qRound(msecs / 1000.0), count, length);
}

//-----------------------------------------------------------------------------

void Window::gamePauseChanged() {
	bool paused = m_board->isPaused();
	m_pause_action->setText(paused ? tr("&Resume") : tr("&Pause"));
	m_clock->setPaused(paused);
}
