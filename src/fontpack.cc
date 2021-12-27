/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#include "fontpack.h"
#include "log.h"
#include "options.h"

#include <ftw.h>
//#include <stdio.h> // DEBUG
#include <string.h>
#include <strings.h>
#include <fontconfig/fontconfig.h>

namespace
{

FcPattern *matchFont(FcConfig *config, FcPattern *pattern)
{
   FcResult result;
   FcPattern *configured = FcPatternDuplicate(pattern);
   if (!configured) return nullptr;
   FcConfigSubstitute(config, configured, FcMatchPattern);
   FcDefaultSubstitute(configured);
   FcPattern *match = FcFontMatch(config, configured, &result);
   FcPatternDestroy(configured);
   return match;
}

}

namespace zutty
{
   Fontpack::Fontpack (const std::string& fontname,
                       const std::string& dwfontname)
   {
      logT << "Fontpack: fontname=\"" << fontname
           << "\"; dwfontname=\"" << dwfontname
           << "\"" << std::endl;

      FcConfig *config = FcInitLoadConfigAndFonts();
      if (!config)
      {
         logE << "Cannot initialize fontconfig library" << std::endl;
         throw std::runtime_error("Cannot initialize fontconfig library");
      }

      // Look for & initialize the regular font (with variants)
      FcPattern *pattern = FcNameParse((const FcChar8 *)fontname.c_str());
      if (!pattern)
      {
         logE << "Cannot parse font reference '" << fontname << "'"
              << std::endl;
         throw std::runtime_error(std::string("Cannot parse font reference '") +
                                  fontname + "'");
      }

      if (opts.dpi > 0)
      {
         FcPatternDel(pattern, FC_DPI);
         FcPatternAddDouble(pattern, FC_DPI, (double)opts.dpi);
      }

      FcPattern *match = matchFont(config, pattern);
      if (!match)
      {
         logE << "Cannot locate regular font file for '" << fontname << "'" << std::endl;
         throw std::runtime_error(std::string("Cannot locate regular font file for '") +
                                  fontname + "'");
      }

      fontRegular = std::make_unique<Font>(match);
      px = fontRegular->getPx ();
      py = fontRegular->getPy ();

      FcPatternDel(pattern, FC_SLANT);
      FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
      match = matchFont(config, pattern);
      if (match)
      {
         fontItalic = std::make_unique<Font>(match, *fontRegular, Font::Overlay);
      }
      else
      {
         logW << "Failed to load italic variant of '" << fontname << "'" << std::endl;
      }

      FcPatternDel(pattern, FC_WEIGHT);
      FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
      match = matchFont(config, pattern);
      if (match)
      {
         fontBoldItalic = std::make_unique<Font>(match, *fontRegular, Font::Overlay);
      }
      else
      {
         logW << "Failed to load bold italic variant of '" << fontname << "'" << std::endl;
      }

      FcPatternDel(pattern, FC_SLANT);
      FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
      match = matchFont(config, pattern);
      if (match)
      {
         fontBold = std::make_unique<Font>(match, *fontRegular, Font::Overlay);
      }
      else
      {
         logW << "Failed to load bold variant of '" << fontname << "'" << std::endl;
      }

      // Look for & initialize the double-width font
      FcPatternDestroy(pattern);
      if (!dwfontname.empty())
      {
         pattern = FcNameParse((const FcChar8 *)dwfontname.c_str());
         if (!pattern)
         {
            logW << "cannot parse double-width font reference '" << dwfontname << "'"
               << std::endl;
            return;
         }
         match = matchFont(config, pattern);
         if (match)
         {
            fontDoubleWidth = std::make_unique<Font>(match, *fontRegular, Font::DoubleWidth);
         }
         else
         {
            logW << "Failed to load double-width font '" << dwfontname << "'" << std::endl;
         }
         FcPatternDestroy(pattern);
      }
   }

} // namespace zutty
