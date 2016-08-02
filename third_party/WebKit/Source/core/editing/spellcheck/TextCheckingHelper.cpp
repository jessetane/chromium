/*
 * Copyright (C) 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/spellcheck/TextCheckingHelper.h"

#include "core/dom/Document.h"
#include "core/dom/Range.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/iterators/CharacterIterator.h"
#include "core/editing/iterators/WordAwareIterator.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/page/SpellCheckerClient.h"
#include "platform/text/TextBreakIterator.h"
#include "platform/text/TextCheckerClient.h"

namespace blink {

static void findMisspellings(TextCheckerClient& client, const UChar* text, int start, int length, Vector<TextCheckingResult>& results)
{
    TextBreakIterator* iterator = wordBreakIterator(text + start, length);
    if (!iterator)
        return;
    int wordStart = iterator->current();
    while (0 <= wordStart) {
        int wordEnd = iterator->next();
        if (wordEnd < 0)
            break;
        int wordLength = wordEnd - wordStart;
        int misspellingLocation = -1;
        int misspellingLength = 0;
        client.checkSpellingOfString(String(text + start + wordStart, wordLength), &misspellingLocation, &misspellingLength);
        if (0 < misspellingLength) {
            DCHECK_LE(0, misspellingLocation);
            DCHECK_LE(misspellingLocation, wordLength);
            DCHECK_LT(0, misspellingLength);
            DCHECK_LE(misspellingLocation + misspellingLength, wordLength);
            TextCheckingResult misspelling;
            misspelling.decoration = TextDecorationTypeSpelling;
            misspelling.location = start + wordStart + misspellingLocation;
            misspelling.length = misspellingLength;
            results.append(misspelling);
        }

        wordStart = wordEnd;
    }
}

static EphemeralRange expandToParagraphBoundary(const EphemeralRange& range)
{
    const VisiblePosition& start = createVisiblePosition(range.startPosition());
    DCHECK(start.isNotNull()) << range.startPosition();
    const VisiblePosition& paragraphStart = startOfParagraph(start);
    DCHECK(paragraphStart.isNotNull()) << range.startPosition();

    const VisiblePosition& end = createVisiblePosition(range.endPosition());
    DCHECK(end.isNotNull()) << range.endPosition();
    const VisiblePosition& paragraphEnd = endOfParagraph(end);
    DCHECK(paragraphEnd.isNotNull()) << range.endPosition();

    return EphemeralRange(paragraphStart.deepEquivalent(), paragraphEnd.deepEquivalent());
}

TextCheckingParagraph::TextCheckingParagraph(const EphemeralRange& checkingRange)
    : m_checkingRange(checkingRange)
    , m_checkingStart(-1)
    , m_checkingEnd(-1)
    , m_checkingLength(-1)
{
}

TextCheckingParagraph::TextCheckingParagraph(const EphemeralRange& checkingRange, const EphemeralRange& paragraphRange)
    : m_checkingRange(checkingRange)
    , m_paragraphRange(paragraphRange)
    , m_checkingStart(-1)
    , m_checkingEnd(-1)
    , m_checkingLength(-1)
{
}

TextCheckingParagraph::TextCheckingParagraph(Range* checkingRange, Range* paragraphRange)
    : m_checkingRange(checkingRange)
    , m_paragraphRange(paragraphRange)
    , m_checkingStart(-1)
    , m_checkingEnd(-1)
    , m_checkingLength(-1)
{
}

TextCheckingParagraph::~TextCheckingParagraph()
{
}

void TextCheckingParagraph::expandRangeToNextEnd()
{
    DCHECK(m_checkingRange.isNotNull());
    setParagraphRange(EphemeralRange(paragraphRange().startPosition(), endOfParagraph(startOfNextParagraph(createVisiblePosition(paragraphRange().startPosition()))).deepEquivalent()));
    invalidateParagraphRangeValues();
}

void TextCheckingParagraph::invalidateParagraphRangeValues()
{
    m_checkingStart = m_checkingEnd = -1;
    m_offsetAsRange = EphemeralRange();
    m_text = String();
}

int TextCheckingParagraph::rangeLength() const
{
    DCHECK(m_checkingRange.isNotNull());
    return TextIterator::rangeLength(paragraphRange().startPosition(), paragraphRange().endPosition());
}

EphemeralRange TextCheckingParagraph::paragraphRange() const
{
    DCHECK(m_checkingRange.isNotNull());
    if (m_paragraphRange.isNull())
        m_paragraphRange = expandToParagraphBoundary(checkingRange());
    return m_paragraphRange;
}

void TextCheckingParagraph::setParagraphRange(const EphemeralRange& range)
{
    m_paragraphRange = range;
}

EphemeralRange TextCheckingParagraph::subrange(int characterOffset, int characterCount) const
{
    DCHECK(m_checkingRange.isNotNull());
    return calculateCharacterSubrange(paragraphRange(), characterOffset, characterCount);
}

int TextCheckingParagraph::offsetTo(const Position& position) const
{
    DCHECK(m_checkingRange.isNotNull());
    return TextIterator::rangeLength(offsetAsRange().startPosition(), position);
}

bool TextCheckingParagraph::isEmpty() const
{
    // Both predicates should have same result, but we check both just to be sure.
    // We need to investigate to remove this redundancy.
    return isRangeEmpty() || isTextEmpty();
}

EphemeralRange TextCheckingParagraph::offsetAsRange() const
{
    DCHECK(m_checkingRange.isNotNull());
    if (m_offsetAsRange.isNotNull())
        return m_offsetAsRange;
    const Position& paragraphStart = paragraphRange().startPosition();
    const Position& checkingStart = checkingRange().startPosition();
    if (paragraphStart <= checkingStart) {
        m_offsetAsRange = EphemeralRange(paragraphStart, checkingStart);
        return m_offsetAsRange;
    }
    // editing/pasteboard/paste-table-001.html and more reach here.
    m_offsetAsRange = EphemeralRange(checkingStart, paragraphStart);
    return m_offsetAsRange;
}

const String& TextCheckingParagraph::text() const
{
    DCHECK(m_checkingRange.isNotNull());
    if (m_text.isEmpty())
        m_text = plainText(paragraphRange());
    return m_text;
}

int TextCheckingParagraph::checkingStart() const
{
    DCHECK(m_checkingRange.isNotNull());
    if (m_checkingStart == -1)
        m_checkingStart = TextIterator::rangeLength(offsetAsRange().startPosition(), offsetAsRange().endPosition());
    return m_checkingStart;
}

int TextCheckingParagraph::checkingEnd() const
{
    DCHECK(m_checkingRange.isNotNull());
    if (m_checkingEnd == -1)
        m_checkingEnd = checkingStart() + TextIterator::rangeLength(checkingRange().startPosition(), checkingRange().endPosition());
    return m_checkingEnd;
}

int TextCheckingParagraph::checkingLength() const
{
    DCHECK(m_checkingRange.isNotNull());
    if (-1 == m_checkingLength)
        m_checkingLength = TextIterator::rangeLength(checkingRange().startPosition(), checkingRange().endPosition());
    return m_checkingLength;
}

TextCheckingHelper::TextCheckingHelper(SpellCheckerClient& client, const Position& start, const Position& end)
    : m_client(&client)
    , m_start(start)
    , m_end(end)
{
}

TextCheckingHelper::~TextCheckingHelper()
{
}

String TextCheckingHelper::findFirstMisspelling(int& firstMisspellingOffset, bool markAll)
{
    // TODO(dglazkov): The use of updateStyleAndLayoutIgnorePendingStylesheets needs to be audited.
    // see http://crbug.com/590369 for more details.
    m_start.document()->updateStyleAndLayoutIgnorePendingStylesheets();

    WordAwareIterator it(m_start, m_end);
    firstMisspellingOffset = 0;

    String firstMisspelling;
    int currentChunkOffset = 0;

    while (!it.atEnd()) {
        int length = it.length();

        // Skip some work for one-space-char hunks
        if (!(length == 1 && it.characterAt(0) == ' ')) {

            int misspellingLocation = -1;
            int misspellingLength = 0;
            m_client->textChecker().checkSpellingOfString(it.substring(0, length), &misspellingLocation, &misspellingLength);

            // 5490627 shows that there was some code path here where the String constructor below crashes.
            // We don't know exactly what combination of bad input caused this, so we're making this much
            // more robust against bad input on release builds.
            DCHECK_GE(misspellingLength, 0);
            DCHECK_GE(misspellingLocation, -1);
            DCHECK(!misspellingLength || misspellingLocation >= 0);
            DCHECK_LT(misspellingLocation, length);
            DCHECK_LE(misspellingLength, length);
            DCHECK_LE(misspellingLocation + misspellingLength, length);

            if (misspellingLocation >= 0 && misspellingLength > 0 && misspellingLocation < length && misspellingLength <= length && misspellingLocation + misspellingLength <= length) {

                // Compute range of misspelled word
                const EphemeralRange misspellingRange = calculateCharacterSubrange(EphemeralRange(m_start, m_end), currentChunkOffset + misspellingLocation, misspellingLength);

                // Remember first-encountered misspelling and its offset.
                if (!firstMisspelling) {
                    firstMisspellingOffset = currentChunkOffset + misspellingLocation;
                    firstMisspelling = it.substring(misspellingLocation, misspellingLength);
                }

                // Store marker for misspelled word.
                misspellingRange.document().markers().addMarker(misspellingRange.startPosition(), misspellingRange.endPosition(), DocumentMarker::Spelling);

                // Bail out if we're marking only the first misspelling, and not all instances.
                if (!markAll)
                    break;
            }
        }

        currentChunkOffset += length;
        it.advance();
    }

    return firstMisspelling;
}

String TextCheckingHelper::findFirstMisspellingOrBadGrammar(bool& outIsSpelling, int& outFirstFoundOffset, GrammarDetail& outGrammarDetail)
{
    if (!unifiedTextCheckerEnabled())
        return "";

    String firstFoundItem;
    String misspelledWord;
    String badGrammarPhrase;

    // Initialize out parameters; these will be updated if we find something to return.
    outIsSpelling = true;
    outFirstFoundOffset = 0;
    outGrammarDetail.location = -1;
    outGrammarDetail.length = 0;
    outGrammarDetail.guesses.clear();
    outGrammarDetail.userDescription = "";

    // Expand the search range to encompass entire paragraphs, since text checking needs that much context.
    // Determine the character offset from the start of the paragraph to the start of the original search range,
    // since we will want to ignore results in this area.
    Position paragraphStart = startOfParagraph(createVisiblePosition(m_start)).toParentAnchoredPosition();
    Position paragraphEnd = m_end;
    int totalRangeLength = TextIterator::rangeLength(paragraphStart, paragraphEnd);
    paragraphEnd = endOfParagraph(createVisiblePosition(m_start)).toParentAnchoredPosition();

    int rangeStartOffset = TextIterator::rangeLength(paragraphStart, m_start);
    int totalLengthProcessed = 0;

    bool firstIteration = true;
    bool lastIteration = false;
    while (totalLengthProcessed < totalRangeLength) {
        // Iterate through the search range by paragraphs, checking each one for spelling.
        int currentLength = TextIterator::rangeLength(paragraphStart, paragraphEnd);
        int currentStartOffset = firstIteration ? rangeStartOffset : 0;
        int currentEndOffset = currentLength;
        if (inSameParagraph(createVisiblePosition(paragraphStart), createVisiblePosition(m_end))) {
            // Determine the character offset from the end of the original search range to the end of the paragraph,
            // since we will want to ignore results in this area.
            currentEndOffset = TextIterator::rangeLength(paragraphStart, m_end);
            lastIteration = true;
        }
        if (currentStartOffset < currentEndOffset) {
            String paragraphString = plainText(EphemeralRange(paragraphStart, paragraphEnd));
            if (paragraphString.length() > 0) {
                int spellingLocation = 0;

                Vector<TextCheckingResult> results;
                TextCheckingTypeMask checkingTypes = TextCheckingTypeSpelling;
                checkTextOfParagraph(m_client->textChecker(), paragraphString, checkingTypes, results);

                for (unsigned i = 0; i < results.size(); i++) {
                    const TextCheckingResult* result = &results[i];
                    if (result->decoration == TextDecorationTypeSpelling && result->location >= currentStartOffset && result->location + result->length <= currentEndOffset) {
                        DCHECK_GT(result->length, 0);
                        DCHECK_GE(result->location, 0);
                        spellingLocation = result->location;
                        misspelledWord = paragraphString.substring(result->location, result->length);
                        DCHECK(misspelledWord.length());
                        break;
                    }
                }

                if (!misspelledWord.isEmpty()) {
                    int spellingOffset = spellingLocation - currentStartOffset;
                    if (!firstIteration)
                        spellingOffset += TextIterator::rangeLength(m_start, paragraphStart);
                    outIsSpelling = true;
                    outFirstFoundOffset = spellingOffset;
                    firstFoundItem = misspelledWord;
                    break;
                }
            }
        }
        if (lastIteration || totalLengthProcessed + currentLength >= totalRangeLength)
            break;
        VisiblePosition newParagraphStart = startOfNextParagraph(createVisiblePosition(paragraphEnd));
        paragraphStart = newParagraphStart.toParentAnchoredPosition();
        paragraphEnd = endOfParagraph(newParagraphStart).toParentAnchoredPosition();
        firstIteration = false;
        totalLengthProcessed += currentLength;
    }
    return firstFoundItem;
}

String TextCheckingHelper::findFirstBadGrammar(GrammarDetail& outGrammarDetail, int& outGrammarPhraseOffset, bool markAll)
{
    // Return empty result since there is no grammar checking.
    outGrammarDetail.location = -1;
    outGrammarDetail.length = 0;
    outGrammarDetail.guesses.clear();
    outGrammarDetail.userDescription = "";
    outGrammarPhraseOffset = 0;
    return "";
}

bool TextCheckingHelper::markAllMisspellings()
{
    // Use the "markAll" feature of findFirstMisspelling. Ignore the return value and the "out parameter";
    // all we need to do is mark every instance.
    int ignoredOffset;
    return findFirstMisspelling(ignoredOffset, true).isEmpty();
}

void TextCheckingHelper::markAllBadGrammar()
{
}

bool TextCheckingHelper::unifiedTextCheckerEnabled() const
{
    DCHECK(m_start.isNotNull());
    Document& doc = m_start.computeContainerNode()->document();
    return blink::unifiedTextCheckerEnabled(doc.frame());
}

void checkTextOfParagraph(TextCheckerClient& client, const String& text, TextCheckingTypeMask checkingTypes, Vector<TextCheckingResult>& results)
{
    Vector<UChar> characters;
    text.appendTo(characters);
    unsigned length = text.length();

    Vector<TextCheckingResult> spellingResult;
    if (checkingTypes & TextCheckingTypeSpelling)
        findMisspellings(client, characters.data(), 0, length, spellingResult);

    if (spellingResult.size())
        results.swap(spellingResult);
}

bool unifiedTextCheckerEnabled(const LocalFrame* frame)
{
    if (!frame)
        return false;

    const Settings* settings = frame->settings();
    if (!settings)
        return false;

    return settings->unifiedTextCheckerEnabled();
}

} // namespace blink
