/***********************************************************************
 *
 * Copyright (C) 2009, 2012, 2013 Graeme Gott <graeme@gottcode.org>
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
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>

Window::Window() {
	setWindowTitle(QCoreApplication::applicationName());

	m_board = new Board(this);
	connect(m_board, SIGNAL(finished()), this, SLOT(gameFinished()));
	connect(m_board, SIGNAL(started()), this, SLOT(gameStarted()));
	connect(m_board, SIGNAL(pauseChanged()), this, SLOT(gamePauseChanged()));

	QWidget* contents = new QWidget(this);
	setCentralWidget(contents);

	View* view = new View(m_board, contents);

	m_scores = new ScoreBoard(this);

	m_definitions = new Definitions(m_board->words(), this);
	connect(m_board, SIGNAL(wordAdded(QString)), m_definitions, SLOT(addWord(QString)));
	connect(m_board, SIGNAL(wordSolved(QString, QString)), m_definitions, SLOT(solveWord(QString, QString)));
	connect(m_board, SIGNAL(wordSelected(QString)), m_definitions, SLOT(selectWord(QString)));
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
	menu->addAction(tr("&Choose..."), this, SLOT(chooseGame()));
	menu->addSeparator();
	m_pause_action = menu->addAction(tr("&Pause"), m_board, SLOT(togglePaused()), tr("P"));
	m_pause_action->setDisabled(true);
	QAction* hint_action = menu->addAction(tr("&Hint"), m_board, SLOT(showHint()), tr("H"));
	hint_action->setDisabled(true);
	connect(m_board, SIGNAL(hintAvailable(bool)), hint_action, SLOT(setEnabled(bool)));
	menu->addAction(tr("D&efinitions"), m_definitions, SLOT(selectWord()), tr("D"));
	menu->addSeparator();
	menu->addAction(tr("&Details"), this, SLOT(showDetails()));
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
	if (settings.contains("Current/Words") && (settings.value("Current/Version" ).toInt() == 3)) {
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

void Window::chooseGame() {
	bool ok = false;
	QString number = QInputDialog::getText(this, tr("Choose Game"), tr("Game Number:"), QLineEdit::Normal, QString(), &ok);
	if (ok && !number.isEmpty()) {
		number = number.trimmed();
		if (!m_board->openGame(number)) {
			QMessageBox::warning(this, tr("Sorry"), tr("Unable to start requested game."));
		}
	}
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
	QMessageBox::about(this, tr("About"), QString("<p><center><big><b>%1 %2</b></big><br/>%3<br/><small>%4<br/>%5</small></center></p><p><center>%6</center></p>")
		.arg(tr("Connectagram"), QCoreApplication::applicationVersion(),
			tr("A word unscrambling game"),
			tr("Copyright &copy; 2009-%1 by Graeme Gott").arg("2013"),
			tr("Released under the <a href=\"http://www.gnu.org/licenses/gpl.html\">GPL 3</a> license"),
			tr("Definitions are from <a href=\"http://wiktionary.org/\">Wiktionary</a>"))
	);
}

//-----------------------------------------------------------------------------

void Window::showDetails() {
	Pattern* pattern = m_board->pattern();
	if (!pattern) {
		return;
	}
	QString patternid = QSettings().value("Current/Pattern", 0).toString();
	static const QStringList sizes = QStringList() << NewGameDialog::tr("Low")
		<< NewGameDialog::tr("Medium")
		<< NewGameDialog::tr("High")
		<< NewGameDialog::tr("Very High");
	QString number = "3"
		+ m_board->words()->language()
		+ patternid
		+ QString::number(pattern->wordCount())
		+ QString("%1").arg(int(pattern->wordLength() - 4), 2, 16, QLatin1Char('0'))
		+ QString::number(pattern->seed(), 16);
	QMessageBox dialog(QMessageBox::Information,
		tr("Details"),
		QString("<p><b>%1</b> %2<br><b>%3</b> %4<br><b>%5</b> %6<br><b>%7</b> %8<br><b>%9</b> %10</p>")
			.arg(tr("Pattern:")).arg(pattern->name())
			.arg(NewGameDialog::tr("Language:")).arg(LocaleDialog::languageName(m_board->words()->language()))
			.arg(NewGameDialog::tr("Amount of Words:")).arg(sizes.value(pattern->wordCount()))
			.arg(NewGameDialog::tr("Word Length:")).arg(NewGameDialog::tr("%n letter(s)", "", pattern->wordLength() + 1))
			.arg(tr("Game Number:")).arg(number),
		QMessageBox::NoButton,
		this);
	dialog.setIconPixmap(QString(":/patterns/%1.png").arg(patternid));
	dialog.exec();
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
