/***********************************************************************
 *
 * Copyright (C) 2009 Graeme Gott <graeme@gottcode.org>
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

#include "pattern.h"

#include "word.h"

#include <QFile>
#include <QMutexLocker>
#include <QRegExp>
#include <QStringList>
#include <QTextStream>
#include <QVariant>

#include <algorithm>
#include <cstdlib>

QHash<int, QStringList> Pattern::m_words;
int Pattern::m_max_length = 0;

//-----------------------------------------------------------------------------

Pattern::Pattern()
: m_current(0,0), m_count(0), m_length(0), m_seed(0), m_cancelled(false) {
	if (m_words.isEmpty()) {
		QFile file("words");
		if (!file.open(QFile::ReadOnly | QIODevice::Text)) {
			return;
		}

		QTextStream in(&file);
		while (!in.atEnd()) {
			QString line = in.readLine();
			m_max_length = qMax(m_max_length, line.length());
			if (line.length() > 4) {
				m_words[line.length() - 1].append(line.toUpper());
			}
		}
	}
}

//-----------------------------------------------------------------------------

Pattern::~Pattern() {
	if (isRunning()) {
		m_cancelled_mutex.lock();
		m_cancelled = true;
		m_cancelled_mutex.unlock();
		wait();
	}
	cleanUp();
}

//-----------------------------------------------------------------------------

void Pattern::setCount(int count) {
	m_count = qMax(count, minimumCount());
	m_count -= ((m_count - minimumCount()) % expandCount());
}

//-----------------------------------------------------------------------------

void Pattern::setLength(int length) {
	m_length = qBound(minimumLength(), length, maximumLength()) - 1;
}

//-----------------------------------------------------------------------------

void Pattern::setSeed(int seed) {
	m_seed = seed;
}

//-----------------------------------------------------------------------------

Pattern* Pattern::create(int type) {
	Pattern* pattern = 0;
	switch (type) {
		case 0:
			pattern = new ChainPattern;
			break;
		case 1:
			pattern = new FencePattern;
			break;
		case 2:
			pattern = new RingsPattern;
			break;
		case 3:
			pattern = new StairsPattern;
			break;
		case 4:
			pattern = new TwistyPattern;
			break;
		case 5:
			pattern = new WavePattern;
			break;
		default:
			break;
	}
	return pattern;
}

//-----------------------------------------------------------------------------

Word* Pattern::addRandomWord(Qt::Orientation orientation) {
	// Filter words on what characters are on the board
	QString filter;
	QPoint pos = m_current;
	QPoint delta = (orientation == Qt::Horizontal) ? QPoint(1, 0) : QPoint(0, 1);
	for (int i = 0; i <= wordLength(); ++i) {
		QChar c = at(pos);
		filter.append(c.isNull() ? QChar('.') : c);
		pos += delta;
	}
	QStringList filtered = m_words.value(wordLength()).filter(QRegExp(filter));

	// Remove anagrams of words on the board
	QStringList words;
	QString sorted;
	bool anagram;
	foreach (const QString& word, filtered) {
		anagram = false;
		sorted = word;
		std::sort(sorted.begin(), sorted.end());
		foreach (const QString& skip, m_skip) {
			if (sorted == skip) {
				anagram = true;
				break;
			}
		}
		if (!anagram) {
			words.append(word);
		}
	}

	// Find word
	QString result = !words.isEmpty() ? words.at(qrand() % words.count()) : QString();
	if (result.isEmpty()) {
		return 0;
	}
	Word* word = new Word(result, m_current, orientation);
	std::sort(result.begin(), result.end());
	m_skip.append(result);
	return word;
}

//-----------------------------------------------------------------------------

QChar Pattern::at(const QPoint& pos) const {
	foreach (Word* word, m_solution) {
		QList<QPoint> positions = word->positions();
		for (int i = 0; i < positions.count(); ++i) {
			if (positions.at(i) == pos) {
				return word->at(i);
			}
		}
	}
	return QChar();
}

//-----------------------------------------------------------------------------

void Pattern::run() {
	if (m_words.isEmpty()) {
		return;
	}

	qsrand(m_seed);

	// Add words
	cleanUp();
	int s = steps();
	for (int i = 0; i < wordCount(); ++i) {
		Word* word = addWord(i % s);
		if (word) {
			m_solution.append(word);
		} else {
			cleanUp();
			i = -1;
		}

		QMutexLocker locker(&m_cancelled_mutex);
		if (m_cancelled) {
			return;
		}
	}

	// Move words so that no positions are negative
	int x1 = 0x7FFFFFFF;
	int x2 = -x1;
	int y1 = x1;
	int y2 = x2;
	foreach (Word* word, m_solution) {
		QList<QPoint> positions = word->positions();
		foreach (const QPoint& pos, positions) {
			x1 = qMin(x1, pos.x());
			y1 = qMin(y1, pos.y());
			x2 = qMax(x2, pos.x());
			y2 = qMax(y2, pos.y());
		}
	}
	m_size = QSize(x2 - x1 + 1, y2 - y1 + 1);

	QPoint delta = QPoint(-x1, -y1);
	foreach (Word* word, m_solution) {
		word->moveBy(delta);
	}

	emit generated();
}

//-----------------------------------------------------------------------------

void Pattern::cleanUp() {
	qDeleteAll(m_solution);
	m_solution.clear();
	m_skip.clear();
	m_current = QPoint(0,0);
}

//-----------------------------------------------------------------------------

Word* Pattern::addWord(int) {
	return 0;
}

//-----------------------------------------------------------------------------

Word* ChainPattern::addWord(int step) {
	Word* result = 0;
	switch (step) {
		case 0:
			result = addRandomWord(Qt::Vertical);
			break;
		case 1:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength(), 0);
			break;
		case 2:
			result = addRandomWord(Qt::Vertical);
			m_current += QPoint(-wordLength(), wordLength());
			break;
		case 3:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength() - 1, wordLength() / -2);
			break;
		case 4:
			result = addRandomWord(Qt::Horizontal);
			m_current = QPoint(m_current.x() + wordLength() - 1, 0);
			break;
		default:
			break;
	}
	return result;
}

//-----------------------------------------------------------------------------

Word* FencePattern::addWord(int step) {
	Word* result = 0;
	switch (step) {
		case 0:
			result = addRandomWord(Qt::Vertical);
			m_current += QPoint(0, 1);
			break;
		case 1:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(0, 2);
			break;
		case 2:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength(), -3);
			break;
		case 3:
			result = addRandomWord(Qt::Vertical);
			break;
		case 4:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(0, 2);
			break;
		case 5:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength(),- 2);
			break;
		default:
			break;
	}
	return result;
}

//-----------------------------------------------------------------------------

Word* RingsPattern::addWord(int step) {
	Word* result = 0;
	switch (step) {
		case 0:
			result = addRandomWord(Qt::Horizontal);
			break;
		case 1:
			result = addRandomWord(Qt::Vertical);
			m_current += QPoint(0, wordLength());
			break;
		case 2:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength(), -wordLength());
			break;
		case 3:
			result = addRandomWord(Qt::Vertical);
			m_current = QPoint(m_current.x() - 2, m_current.y() ? 0 : (wordLength() - 2));
			break;
		default:
			break;
	}
	return result;
}

//-----------------------------------------------------------------------------

Word* StairsPattern::addWord(int step) {
	Word* result = 0;
	switch (step) {
		case 0:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength() - 1, 0);
			break;
		case 1:
			result = addRandomWord(Qt::Vertical);
			m_current += QPoint(0, wordLength());
			break;
		default:
			break;
	}
	return result;
}

//-----------------------------------------------------------------------------

Word* TwistyPattern::addWord(int step) {
	return step ? stepTwo() : stepOne();
}

//-----------------------------------------------------------------------------

Word* TwistyPattern::stepOne() {
	QList<QPoint> positions;
	for (int i = 0; i <= wordLength(); ++i) {
		positions.append(m_current + QPoint(0, i));
	}

	while (!positions.isEmpty()) {
		QPoint pos = positions.takeAt(qrand() % positions.count());
		int right = wordLength() + pos.x();
		int left = -wordLength() + pos.x();

		for (int x = 1; x <= wordLength(); ++x) {
			for (int y = -1; y < 2; ++y) {
				QPoint test(pos.x() + x, pos.y() + y);
				if (!at(test).isNull()) {
					right = x + pos.x() - 2;
					x = wordLength();
					break;
				}
			}
		}

		for (int x = -1; x >= -wordLength(); --x) {
			for (int y = -1; y < 2; ++y) {
				QPoint test(pos.x() + x, pos.y() + y);
				if (!at(test).isNull()) {
					left = x + pos.x() + 2;
					x = -wordLength();
					break;
				}
			}
		}

		int length = right - left;
		int offset = length - wordLength();
		if (offset >= 0) {
			left += offset ? (qrand() % offset) : 0;
			right = left + wordLength();
			m_current = QPoint(left, pos.y());
			return addRandomWord(Qt::Horizontal);
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------

Word* TwistyPattern::stepTwo() {
	QList<QPoint> positions;
	for (int i = 0; i <= wordLength(); ++i) {
		positions.append(m_current + QPoint(i, 0));
	}

	while (!positions.isEmpty()) {
		QPoint pos = positions.takeAt(qrand() % positions.count());
		int bottom = wordLength() + pos.y();
		int top = -wordLength() + pos.y();

		for (int y = 1; y <= wordLength(); ++y) {
			for (int x = -1; x < 2; ++x) {
				QPoint test(pos.x() + x, pos.y() + y);
				if (!at(test).isNull()) {
					bottom = y + pos.y() - 2;
					y = wordLength();
					break;
				}
			}
		}

		for (int y = -1; y >= -wordLength(); --y) {
			for (int x = -1; x < 2; ++x) {
				QPoint test(pos.x() + x, pos.y() + y);
				if (!at(test).isNull()) {
					top = y + pos.y() + 2;
					y = -wordLength();
					break;
				}
			}
		}

		int length = bottom - top;
		int offset = length - wordLength();
		if (offset >= 0) {
			top += offset ? (qrand() % offset) : 0;
			bottom = top + wordLength();
			m_current = QPoint(pos.x(), top);
			return addRandomWord(Qt::Vertical);
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------

Word* WavePattern::addWord(int step) {
	Word* result = 0;
	switch (step) {
		case 0:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength(), 0);
			break;
		case 1:
			result = addRandomWord(Qt::Vertical);
			m_current += QPoint(0, wordLength());
			break;
		case 2:
			result = addRandomWord(Qt::Horizontal);
			m_current += QPoint(wordLength(), -wordLength());
			break;
		case 3:
			result = addRandomWord(Qt::Vertical);
			break;
		default:
			break;
	}
	return result;
}