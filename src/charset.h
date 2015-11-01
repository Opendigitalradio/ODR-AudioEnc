/*
    Copyright (C) 2015 Matthias P. Braendli (http://opendigitalradio.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    charset.h
         Define the EBU charset according to ETSI TS 101 756v1.8.1 for DLS encoding

    Authors:
         Matthias P. Braendli <matthias@mpb.li>
         Lindsay Cornell
*/

#ifndef __CHARSET_H_
#define __CHARSET_H_

#include "utf8.h"
#include <string>
#include <vector>
#include <algorithm>

/**********************************************/
/************* BIG FAT WARNING ****************/
/**********************************************/
/**** Make sure this file is always saved  ****/
/**** encoded in UTF-8, otherwise you will ****/
/****      mess up the table below !       ****/
/**********************************************/
/********* END OF BIG FAT WARNING *************/
/**********************************************/


#define CHARSET_TABLE_OFFSET 1 // NUL at index 0 cannot be represented
#define CHARSET_TABLE_ENTRIES (256 - CHARSET_TABLE_OFFSET)
const char* utf8_encoded_EBU_Latin[CHARSET_TABLE_ENTRIES] = {
     "Ę", "Į", "Ų", "Ă", "Ė", "Ď", "Ș", "Ț", "Ċ", "\n","\v","Ġ", "Ĺ", "Ż", "Ń",
"ą", "ę", "į", "ų", "ă", "ė", "ď", "ș", "ț", "ċ", "Ň", "Ě", "ġ", "ĺ", "ż", "\u0082",
" ", "!", "\"","#", "ł", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/",
"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", "=", ">", "?",
"@", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "[", "Ů", "]", "Ł", "_",
"Ą", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o",
"p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "«", "ů", "»", "Ľ", "Ħ",
"á", "à", "é", "è", "í", "ì", "ó", "ò", "ú", "ù", "Ñ", "Ç", "Ş", "ß", "¡", "Ÿ",
"â", "ä", "ê", "ë", "î", "ï", "ô", "ö", "û", "ü", "ñ", "ç", "ş", "ğ", "ı", "ÿ",
"Ķ", "Ņ", "©", "Ģ", "Ğ", "ě", "ň", "ő", "Ő", "€", "£", "$", "Ā", "Ē", "Ī", "Ū",
"ķ", "ņ", "Ļ", "ģ", "ļ", "İ", "ń", "ű", "Ű", "¿", "ľ", "°", "ā", "ē", "ī", "ū",
"Á", "À", "É", "È", "Í", "Ì", "Ó", "Ò", "Ú", "Ù", "Ř", "Č", "Š", "Ž", "Ð", "Ŀ",
"Â", "Ä", "Ê", "Ë", "Î", "Ï", "Ô", "Ö", "Û", "Ü", "ř", "č", "š", "ž", "đ", "ŀ",
"Ã", "Å", "Æ", "Œ", "ŷ", "Ý", "Õ", "Ø", "Þ", "Ŋ", "Ŕ", "Ć", "Ś", "Ź", "Ť", "ð",
"ã", "å", "æ", "œ", "ŵ", "ý", "õ", "ø", "þ", "ŋ", "ŕ", "ć", "ś", "ź", "ť", "ħ"};

class CharsetConverter
{
    public:
        CharsetConverter() {
            /* Build the converstion table that contains the known code points,
             * at the indices corresponding to the EBU Latin table
             */
            using namespace std;
            for (size_t i = 0; i < CHARSET_TABLE_ENTRIES; i++) {
                string table_entry(utf8_encoded_EBU_Latin[i]);
                string::iterator it = table_entry.begin();
                uint32_t code_point = utf8::next(it, table_entry.end());
                m_conversion_table.push_back(code_point);
            }
        }

        /* Convert a UTF-8 encoded text line into an EBU Latin encoded byte stream
         */
        std::string convert(std::string line_utf8) {
            using namespace std;

            // check for invalid utf-8, we only convert up to the first error
            string::iterator end_it = utf8::find_invalid(line_utf8.begin(), line_utf8.end());

            // Convert it to utf-32
            vector<uint32_t> utf32line;
            utf8::utf8to32(line_utf8.begin(), end_it, back_inserter(utf32line));

            string encoded_line(utf32line.size(), '0');

            // Try to convert each codepoint
            for (size_t i = 0; i < utf32line.size(); i++) {
                vector<uint32_t>::iterator iter = find(m_conversion_table.begin(),
                        m_conversion_table.end(), utf32line[i]);
                if (iter != m_conversion_table.end()) {
                    size_t index = std::distance(m_conversion_table.begin(), iter);

                    encoded_line[i] = (char)(index + CHARSET_TABLE_OFFSET);
                }
                else {
                    encoded_line[i] = ' ';
                }
            }
            return encoded_line;
        }

    private:

        std::vector<uint32_t> m_conversion_table;
};

#endif
