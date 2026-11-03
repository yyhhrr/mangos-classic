/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef MANGOS_AUTH_SESSIONKEY_H
#define MANGOS_AUTH_SESSIONKEY_H

#include "Auth/BigNumber.h"
#include <algorithm>
#include <string>
#include <openssl/crypto.h>

namespace SessionKey
{
    constexpr std::size_t HexLength = 80;
    constexpr int ByteLength = 40;

    // BN_bn2hex has historically produced even-length numeric encodings.
    // Use an ASCII check before parsing: BN_hex2bn also accepts signs/prefixes.
    inline bool Parse(std::string const& text, BigNumber& key)
    {
        if (text.empty() || text.size() > HexLength || text.size() % 2 != 0)
            return false;
        if (!std::all_of(text.begin(), text.end(), [](char c)
            { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }))
            return false;

        BigNumber parsed;
        if (parsed.SetHexStr(text.c_str()) != static_cast<int>(text.size()) || parsed.isZero() || parsed.GetNumBytes() > ByteLength)
            return false;
        key = parsed;
        return true;
    }

    inline std::string ToHex(BigNumber const& key)
    {
        if (key.isZero() || key.GetNumBytes() > ByteLength)
            return {};
        char const* raw = key.AsHexStr();
        if (!raw)
            return {};
        std::string text(raw);
        OPENSSL_free(const_cast<char*>(raw));
        if (text.empty() || text.front() == '-' || text.size() > HexLength)
            return {};
        text.insert(0, HexLength - text.size(), '0');
        return text;
    }
}

#endif
