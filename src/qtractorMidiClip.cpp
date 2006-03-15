// qtractorMidiClip.cpp
//
/****************************************************************************
   Copyright (C) 2005-2006, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*****************************************************************************/

#include "qtractorAbout.h"
#include "qtractorMidiClip.h"
#include "qtractorMidiFile.h"
#include "qtractorMidiEngine.h"

#include "qtractorSessionDocument.h"

#include <qfileinfo.h>
#include <qpainter.h>


//----------------------------------------------------------------------
// class qtractorMidiClip -- Audio file/buffer clip.
//

// Constructor.
qtractorMidiClip::qtractorMidiClip ( qtractorTrack *pTrack )
	: qtractorClip(pTrack)
{
	m_pFile = NULL;
	m_pSeq  = new qtractorMidiSequence();
	m_iTrackChannel = 0;
}

// Copy constructor.
qtractorMidiClip::qtractorMidiClip ( const qtractorMidiClip& clip )
	: qtractorClip(clip.track())
{
	m_pFile = NULL;
	m_pSeq  = new qtractorMidiSequence();
	m_iTrackChannel = 0;

	open(clip.filename(), clip.trackChannel());
}


// Destructor.
qtractorMidiClip::~qtractorMidiClip (void)
{
	if (m_pSeq)
		delete m_pSeq;
	if (m_pFile)
		delete m_pFile;
}


// The main use method.
bool qtractorMidiClip::open ( const QString& sFilename, int iTrackChannel,
	int iMode )
{
	if (m_pFile)
		delete m_pFile;
	
	// Create and open up the MIDI file...
	m_pFile = new qtractorMidiFile();
	if (!m_pFile->open(sFilename, iMode)) {
		delete m_pFile;
		m_pFile = NULL;
		return false;
	}

	// Open-process mode...
	return open(m_pFile, iTrackChannel, false);
}


// Overloaded open method; reuse an already open MIDI file.
bool qtractorMidiClip::open ( qtractorMidiFile *pFile, int iTrackChannel,
	bool bSetTempo )
{
	if (track() == NULL)
		return false;

	qtractorSession *pSession = track()->session();
	if (pSession == NULL)
		return false;

	// Initialize event container...
	m_pSeq->clear();
	m_pSeq->setTicksPerBeat(pSession->ticksPerBeat());

	// Are we on a pre-writing status?
	if (pFile->mode() == qtractorMidiFile::Write) {
		// Set initial local properties...
		pFile->setTempo(pSession->tempo());
		pFile->setBeatsPerBar(pSession->beatsPerBar());
		// And initial clip name...
		m_pSeq->setName(QFileInfo(pFile->filename()).baseName());
		m_pSeq->setChannel(iTrackChannel);
		// Nothing more as for writing...
	} else {	
		// Read the event sequence in...
		if (!pFile->readTrack(m_pSeq, iTrackChannel))
			return false;
		// FIXME: On demand, set session time properties from MIDI file...
		if (bSetTempo) {
			pSession->setTempo(pFile->tempo());
			pSession->setBeatsPerBar(pFile->beatsPerBar());
			pSession->updateTimeScale();
		}
		// We must have events, otherwise this clip is of now use...
		if (m_pSeq->events().count() < 1)
			return false;
	}

	// Set local properties...
	m_sFilename = pFile->filename();
	m_iTrackChannel = iTrackChannel;

	// Clip name should be clear about it all.
	qtractorClip::setClipName(m_pSeq->name());
	// Clip length will be whole sequence duration.
	qtractorClip::setClipLength(pSession->frameFromTick(m_pSeq->duration()));

	// Uh oh...
	m_clipCursor.reset(m_pSeq);

	return true;
}


// MIDI file properties accessors.
const QString& qtractorMidiClip::filename (void) const
{
	return m_sFilename;
}

unsigned short qtractorMidiClip::trackChannel (void) const
{
	return m_iTrackChannel;
}


// Sequence properties accessors.
qtractorMidiSequence *qtractorMidiClip::sequence (void) const
{
	return m_pSeq;
}

unsigned short qtractorMidiClip::channel (void) const
{
	return m_pSeq->channel();
}

int qtractorMidiClip::bank (void) const
{
	return m_pSeq->bank();
}

int qtractorMidiClip::program (void) const
{
	return m_pSeq->program();
}


// Intra-clip tick/time positioning seek.
void qtractorMidiClip::ClipCursor::seek ( qtractorMidiSequence *pSeq,
	unsigned long tick )
{
	if (time < tick) {
		// Seek forward...
		if (event == NULL)
			event = pSeq->events().first();
		while (event && event->time() < tick)
			event = event->next();
		time = tick;
	}
	else
	if (time > tick) {
		// Seek backward...
		if (event == NULL)
			event = pSeq->events().last();
		while (event && event->time() > tick)
			event = event->prev();
		time = tick;
	}
}


// Intra-clip tick/time positioning reset.
void qtractorMidiClip::ClipCursor::reset ( qtractorMidiSequence *pSeq )
{
	event = pSeq->events().first();
	time  = 0;
//	if (event)
//		time += event->time();
}


// Intra-clip playback frame positioning.
void qtractorMidiClip::seek ( unsigned long iOffset )
{
#ifdef CONFIG_DEBUG_0
	fprintf(stderr, "qtractorMidiClip::seek(%p, %lu)\n", this, iOffset);
#endif

	qtractorSession *pSession = track()->session();
	if (pSession == NULL)
		return;

	// Seek for the nearest sequence event...
	unsigned long iTimeSeek = pSession->tickFromFrame(iOffset);
	if (iTimeSeek > 0) {
		m_clipCursor.seek(m_pSeq, iTimeSeek);
	} else {
		m_clipCursor.reset(m_pSeq);
	}
}


// Reset clip state.
void qtractorMidiClip::reset ( bool bLooping )
{
#ifdef CONFIG_DEBUG_0
	fprintf(stderr, "qtractorMidiClip::reset(%p, %d)\n", this, (int) bLooping);
#endif

	// Reset to the first sequence event...
	m_clipCursor.reset(m_pSeq);

	// Take the time from loop-start?
	if (bLooping && clipLoopStart() < clipLoopEnd())
		seek(clipLoopStart());
}


// Loop positioning.
void qtractorMidiClip::loop ( unsigned long iLoopStart,
	unsigned long iLoopEnd )
{
#ifdef CONFIG_DEBUG
	fprintf(stderr, "qtractorMidiClip::loop(%p, %lu, %lu)\n",
		this, iLoopStart, iLoopEnd);
#endif

	// Do nothing?
}


// Clip close-commit (record specific)
void qtractorMidiClip::close (void)
{
	qtractorSession *pSession = track()->session();
	if (pSession == NULL)
		return;

	// Actual sequence closure...
	m_pSeq->close();

	// Commit the final clip length...
	setClipLength(pSession->frameFromTick(m_pSeq->duration()));
	
	// Now's time to write the whole thing, maybe as a SMF format 0...
	if (m_pFile && m_pFile->mode() == qtractorMidiFile::Write) {
		m_pFile->writeHeader(0, 1, m_pSeq->ticksPerBeat());
		m_pFile->writeTrack(m_pSeq);
		m_pFile->close();
	}

	// If proven empty, remove the file.
	if (clipLength() == 0)
		QFile::remove(m_sFilename);
}


// Audio clip special process cycle executive.
void qtractorMidiClip::process ( float fGain,
	unsigned long iFrameStart, unsigned long iFrameEnd )
{
#ifdef CONFIG_DEBUG_0
	fprintf(stderr, "qtractorMidiClip::process(%p, %g, %lu, %lu)\n",
		this, fGain, iFrameStart, iFrameEnd);
#endif

	qtractorSession *pSession = track()->session();
	if (pSession == NULL)
		return;

	// Enqueue the requested events...
	unsigned long iTimeClip  = pSession->tickFromFrame(clipStart());
	unsigned long iTimeStart = pSession->tickFromFrame(iFrameStart);
	unsigned long iTimeEnd   = pSession->tickFromFrame(iFrameEnd);

	qtractorMidiEvent *pEvent = m_clipCursor.event;
	while (pEvent) {
		unsigned long iTimeEvent = iTimeClip + pEvent->time();
		if (iTimeEvent > iTimeEnd)
			break;
		if (iTimeEvent >= iTimeStart) {
			pSession->midiEngine()->enqueue(track(),
				pEvent, iTimeEvent, fGain);
		}
		pEvent = pEvent->next();
	}

	if (pEvent)
		m_clipCursor.time = pEvent->time();
}


// Audio clip paint method.
void qtractorMidiClip::drawClip ( QPainter *pPainter, const QRect& clipRect,
	unsigned long iClipOffset )
{
	qtractorSession *pSession = track()->session();
	if (pSession == NULL)
		return;

	// Fill clip background...
#if 0
	pPainter->fillRect(clipRect, track()->background());
#else
	pPainter->setPen(track()->background().dark());
	pPainter->setBrush(track()->background());
	pPainter->drawRect(clipRect);
#endif

	// Draw clip name label...
	QRect rect(clipRect);
	if (iClipOffset > 0)
		rect.setX(rect.x() - pSession->pixelFromFrame(iClipOffset));
	pPainter->drawText(rect,
		Qt::AlignLeft | Qt::AlignTop | Qt::BreakAnywhere,
		clipName());

	// Check maximum note span...
	int iNoteSpan = (m_pSeq->noteMax() - m_pSeq->noteMin()) + 1;
	if (iNoteSpan < 4)
		iNoteSpan = 4;

	unsigned long iTimeStart = pSession->tickFromFrame(iClipOffset);
	unsigned long iTimeEnd
		= iTimeStart + pSession->tickFromPixel(clipRect.width());

	const QColor fg = track()->foreground();
	pPainter->setPen(fg);
	pPainter->setBrush(fg.light());

	int cx = pSession->pixelFromTick(iTimeStart);
	int h1 = clipRect.height() - 4;
	int h  = h1 / iNoteSpan;
	if (h < 4) h = 4;

	qtractorMidiEvent *pEvent = m_pSeq->events().first();
	while (pEvent && pEvent->time() < iTimeEnd) {
		if (pEvent->type() == qtractorMidiEvent::NOTEON
			&& pEvent->time() + pEvent->duration() >= iTimeStart) {
			int x = clipRect.x()
				+ pSession->pixelFromTick(pEvent->time()) - cx;
			int y = clipRect.bottom()
				- (h1 * (pEvent->note() - m_pSeq->noteMin() + 1)) / iNoteSpan;
			int w = pSession->pixelFromTick(pEvent->duration());
			if (h > 4) {
				if (w < 5) w = 5;
				pPainter->fillRect(x, y, w, h - 1, fg);
				pPainter->fillRect(x + 1, y + 1, w - 4, h - 4, fg.light());
			} else {
				if (w < 3) w = 3;
				pPainter->drawRect(x, y, w, h - 1);
			}
		}
		pEvent = pEvent->next();
	}
}


// Virtual document element methods.
bool qtractorMidiClip::loadClipElement (
	qtractorSessionDocument * /* pDocument */, QDomElement *pElement )
{
	QString sFilename;
	unsigned short iTrackChannel = 0;

	// Load track children...
	for (QDomNode nChild = pElement->firstChild();
			!nChild.isNull();
				nChild = nChild.nextSibling()) {
		// Convert node to element...
		QDomElement eChild = nChild.toElement();
		if (eChild.isNull())
			continue;
		// Load track state..
		if (eChild.tagName() == "filename")
			sFilename = eChild.text();
		else if (eChild.tagName() == "track-channel")
			iTrackChannel = eChild.text().toUShort();
	}

	if (sFilename.isEmpty())
		return false;

	return qtractorMidiClip::open(sFilename, iTrackChannel);
}


bool qtractorMidiClip::saveClipElement (
	qtractorSessionDocument *pDocument, QDomElement *pElement )
{
	QDomElement eMidiClip = pDocument->document()->createElement("midi-clip");
	pDocument->saveTextElement("filename",
		m_sFilename, &eMidiClip);
	pDocument->saveTextElement("track-channel",
		QString::number(m_iTrackChannel), &eMidiClip);
	pElement->appendChild(eMidiClip);

	return true;
}


// end of qtractorMidiClip.cpp
