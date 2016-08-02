// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_data_util.h"

#include <algorithm>
#include <vector>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/field_types.h"
#include "third_party/icu/source/common/unicode/uscript.h"

namespace autofill {
namespace data_util {

namespace {
const char* const name_prefixes[] = {
    "1lt",     "1st", "2lt", "2nd",    "3rd",  "admiral", "capt",
    "captain", "col", "cpt", "dr",     "gen",  "general", "lcdr",
    "lt",      "ltc", "ltg", "ltjg",   "maj",  "major",   "mg",
    "mr",      "mrs", "ms",  "pastor", "prof", "rep",     "reverend",
    "rev",     "sen", "st"};

const char* const name_suffixes[] = {"b.a", "ba", "d.d.s", "dds",  "i",   "ii",
                                     "iii", "iv", "ix",    "jr",   "m.a", "m.d",
                                     "ma",  "md", "ms",    "ph.d", "phd", "sr",
                                     "v",   "vi", "vii",   "viii", "x"};

const char* const family_name_prefixes[] = {"d'",  "de",  "del", "der", "di",
                                            "la",  "le",  "mc",  "san", "st",
                                            "ter", "van", "von"};

// The common and non-ambiguous CJK surnames (last names) that have more than
// one character.
const char* common_cjk_multi_char_surnames[] = {
  // Korean, taken from the list of surnames:
  // https://ko.wikipedia.org/wiki/%ED%95%9C%EA%B5%AD%EC%9D%98_%EC%84%B1%EC%94%A8_%EB%AA%A9%EB%A1%9D
  "남궁", "사공", "서문", "선우", "제갈", "황보", "독고", "망절",

  // Chinese, taken from the top 10 Chinese 2-character surnames:
  // https://zh.wikipedia.org/wiki/%E8%A4%87%E5%A7%93#.E5.B8.B8.E8.A6.8B.E7.9A.84.E8.A4.87.E5.A7.93
  // Simplified Chinese (mostly mainland China)
  "欧阳", "令狐", "皇甫", "上官", "司徒", "诸葛", "司马", "宇文", "呼延", "端木",
  // Traditional Chinese (mostly Taiwan)
  "張簡", "歐陽", "諸葛", "申屠", "尉遲", "司馬", "軒轅", "夏侯"
};

// All Korean surnames that have more than one character, even the
// rare/ambiguous ones.
const char* korean_multi_char_surnames[] = {
  "강전", "남궁", "독고", "동방", "망절", "사공", "서문", "선우",
  "소봉", "어금", "장곡", "제갈", "황목", "황보"
};

// Returns true if |set| contains |element|, modulo a final period.
bool ContainsString(const char* const set[],
                    size_t set_size,
                    const base::string16& element) {
  if (!base::IsStringASCII(element))
    return false;

  base::string16 trimmed_element;
  base::TrimString(element, base::ASCIIToUTF16("."), &trimmed_element);

  for (size_t i = 0; i < set_size; ++i) {
    if (base::LowerCaseEqualsASCII(trimmed_element, set[i]))
      return true;
  }

  return false;
}

// Removes common name prefixes from |name_tokens|.
void StripPrefixes(std::vector<base::string16>* name_tokens) {
  std::vector<base::string16>::iterator iter = name_tokens->begin();
  while (iter != name_tokens->end()) {
    if (!ContainsString(name_prefixes, arraysize(name_prefixes), *iter))
      break;
    ++iter;
  }

  std::vector<base::string16> copy_vector;
  copy_vector.assign(iter, name_tokens->end());
  *name_tokens = copy_vector;
}

// Removes common name suffixes from |name_tokens|.
void StripSuffixes(std::vector<base::string16>* name_tokens) {
  while (!name_tokens->empty()) {
    if (!ContainsString(name_suffixes, arraysize(name_suffixes),
                        name_tokens->back())) {
      break;
    }
    name_tokens->pop_back();
  }
}

// Find whether |name| starts with any of the strings from the array
// |prefixes|. The returned value is the length of the prefix found, or 0 if
// none is found.
size_t StartsWithAny(base::StringPiece16 name, const char** prefixes,
                     size_t prefix_count) {
   base::string16 buffer;
   for (size_t i = 0; i < prefix_count; i++) {
     buffer.clear();
     base::UTF8ToUTF16(prefixes[i], strlen(prefixes[i]), &buffer);
     if (base::StartsWith(name, buffer, base::CompareCase::SENSITIVE)) {
       return buffer.size();
     }
   }
   return 0;
}

// Returns true if |c| is a CJK (Chinese, Japanese, Korean) character, for any
// of the CJK alphabets.
bool IsCJK(base::char16 c) {
  static const std::set<UScriptCode> kCjkScripts {
    USCRIPT_HAN, // CJK logographs, used by all 3 (but rarely for Korean)
    USCRIPT_HANGUL, // Korean alphabet
    USCRIPT_KATAKANA, // A Japanese syllabary
    USCRIPT_HIRAGANA, // A Japanese syllabary
    USCRIPT_BOPOMOFO // Chinese semisyllabary, rarely used
  };
  UErrorCode error = U_ZERO_ERROR;
  UScriptCode script = uscript_getScript(c, &error);
  return kCjkScripts.find(script) != kCjkScripts.end();
}

// Returns true if |name| looks like a CJK name (or some kind of mish-mash of
// the three, at least). The name is considered to be a CJK name if it is only
// CJK characters or spaces.
//
// Chinese and Japanese names are usually spelled out using the Han characters
// (logographs), which constitute the "CJK Unified Ideographs" block in Unicode,
// also referred to as Unihan. Korean names are usually spelled out in the
// Korean alphabet (Hangul), although they do have a Han equivalent as well.
bool IsCJKName(const base::string16& name) {
  for (base::char16 c : name) {
    if (!IsCJK(c) && !base::IsUnicodeWhitespace(c)) {
      return false;
    }
  }
  return true;
}

// Returns true if |c| is a Korean Hangul character.
bool IsHangul(base::char16 c) {
  UErrorCode error = U_ZERO_ERROR;
  return uscript_getScript(c, &error) == USCRIPT_HANGUL;
}

// Returns true if |name| looks like a Korean name, made up entirely of Hangul
// characters or spaces.
bool IsHangulName(const base::string16& name) {
  for (base::char16 c : name) {
    if (!IsHangul(c) && !base::IsUnicodeWhitespace(c)) {
      return false;
    }
  }
  return true;
}

// Tries to split a Chinese, Japanese, or Korean name into its given name &
// surname parts, and puts the result in |parts|. If splitting did not work for
// whatever reason, returns false.
bool SplitCJKName(const std::vector<base::string16>& name_tokens,
                  NameParts* parts) {
  // The convention for CJK languages is to put the surname (last name) first,
  // and the given name (first name) second. In a continuous text, there is
  // normally no space between the two parts of the name. When entering their
  // name into a field, though, some people add a space to disambiguate. CJK
  // names (almost) never have a middle name.
  //
  // TODO(crbug.com/89111): Foreign names in Japanese are written in Katakana,
  // with a '・' (KATAKANA MIDDLE DOT U+30FB) character as a separator, with
  // the *western* ordering. e.g. "ビル・ゲイツ" ("biru・geitsu" AKA Bill Gates)
  if (name_tokens.size() == 1) {
    // There is no space between the surname and given name. Try to infer where
    // to separate between the two. Most Chinese and Korean surnames have only
    // one character, but there are a few that have 2. If the name does not
    // start with a surname from a known list, default to 1 character.
    //
    // TODO(crbug.com/89111): Japanese names with no space will be mis-split,
    // since we don't have a list of Japanese last names. In the Han alphabet,
    // it might also be difficult for us to differentiate between Chinese &
    // Japanese names.
    const base::string16& name = name_tokens.front();
    const bool is_korean = IsHangulName(name);
    size_t surname_length = 0;
    if (is_korean && name.size() > 3) {
      // 4-character Korean names are more likely to be 2/2 than 1/3, so use
      // the full list of Korean 2-char surnames. (instead of only the common
      // ones)
      surname_length = std::max<size_t>(
          1, StartsWithAny(name, korean_multi_char_surnames,
                           arraysize(korean_multi_char_surnames)));
    } else {
      // Default to 1 character if the surname is not in
      // |common_cjk_multi_char_surnames|.
      surname_length = std::max<size_t>(
          1, StartsWithAny(name, common_cjk_multi_char_surnames,
                           arraysize(common_cjk_multi_char_surnames)));
    }
    parts->family = name.substr(0, surname_length);
    parts->given = name.substr(surname_length);
    return true;
  }
  if (name_tokens.size() == 2) {
    // The user entered a space between the two name parts. This makes our job
    // easier. Family name first, given name second.
    parts->family = name_tokens[0];
    parts->given = name_tokens[1];
    return true;
  }
  // We don't know what to do if there are more than 2 tokens.
  return false;
}

}  // namespace

NameParts SplitName(const base::string16& name) {
  std::vector<base::string16> name_tokens =
      base::SplitString(name, base::ASCIIToUTF16(" ,"), base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);
  StripPrefixes(&name_tokens);

  NameParts parts;

  // TODO(crbug.com/89111): Hungarian, Tamil, Telugu, and Vietnamese also have
  // the given name before the surname, and should be treated as special cases
  // too.

  // Treat CJK names differently.
  if (IsCJKName(name) && SplitCJKName(name_tokens, &parts)) {
    return parts;
  }

  // Don't assume "Ma" is a suffix in John Ma.
  if (name_tokens.size() > 2)
    StripSuffixes(&name_tokens);

  if (name_tokens.empty()) {
    // Bad things have happened; just assume the whole thing is a given name.
    parts.given = name;
    return parts;
  }

  // Only one token, assume given name.
  if (name_tokens.size() == 1) {
    parts.given = name_tokens[0];
    return parts;
  }

  // 2 or more tokens. Grab the family, which is the last word plus any
  // recognizable family prefixes.
  std::vector<base::string16> reverse_family_tokens;
  reverse_family_tokens.push_back(name_tokens.back());
  name_tokens.pop_back();
  while (name_tokens.size() >= 1 &&
         ContainsString(family_name_prefixes, arraysize(family_name_prefixes),
                        name_tokens.back())) {
    reverse_family_tokens.push_back(name_tokens.back());
    name_tokens.pop_back();
  }

  std::vector<base::string16> family_tokens(reverse_family_tokens.rbegin(),
                                            reverse_family_tokens.rend());
  parts.family = base::JoinString(family_tokens, base::ASCIIToUTF16(" "));

  // Take the last remaining token as the middle name (if there are at least 2
  // tokens).
  if (name_tokens.size() >= 2) {
    parts.middle = name_tokens.back();
    name_tokens.pop_back();
  }

  // Remainder is given name.
  parts.given = base::JoinString(name_tokens, base::ASCIIToUTF16(" "));

  return parts;
}

bool ProfileMatchesFullName(const base::string16 full_name,
                            const autofill::AutofillProfile& profile) {
  const base::string16 kSpace = base::ASCIIToUTF16(" ");
  const base::string16 kPeriodSpace = base::ASCIIToUTF16(". ");

  // First Last
  base::string16 candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
                             profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // First Middle Last
  candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
              profile.GetRawInfo(autofill::NAME_MIDDLE) + kSpace +
              profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // First M Last
  candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
              profile.GetRawInfo(autofill::NAME_MIDDLE_INITIAL) + kSpace +
              profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // First M. Last
  candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
              profile.GetRawInfo(autofill::NAME_MIDDLE_INITIAL) + kPeriodSpace +
              profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  return false;
}

}  // namespace data_util
}  // namespace autofill