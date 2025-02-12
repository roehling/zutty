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

#include "font.h"
#include "log.h"
#include "options.h"
#include "utf8.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include FT_LCD_FILTER_H

namespace
{

std::string locateFontFile(FcPattern *pattern)
{
   std::string file_name;
   if (pattern)
   {
      FcChar8* buf = nullptr;
      FcPatternGetString(pattern, FC_FILE, 0, &buf);
      if (buf) file_name = std::string((const char *)buf);
   }
   return file_name;
}

constexpr unsigned int bytes_per_pixel = 4; // RGBA

}

namespace zutty
{
   Font::Font (FcPattern* font_)
      : font (font_, [](FcPattern* p) { FcPatternDestroy(p); })
      , overlay (false)
   {
      load ();
   }

   Font::Font (FcPattern* font_, const Font& priFont, Overlay_)
      : font (font_, [](FcPattern* p) { FcPatternDestroy(p); })
      , overlay (true)
      , px (priFont.getPx ())
      , py (priFont.getPy ())
      , baseline (priFont.getBaseline ())
      , nx (priFont.getNx ())
      , ny (priFont.getNy ())
      , atlasBuf (priFont.getAtlas ())
      , atlasMap (priFont.getAtlasMap ())
   {
      load ();
   }

   Font::Font (FcPattern* font_, const Font& priFont, DoubleWidth_)
      : font (font_, [](FcPattern* p) { FcPatternDestroy(p); })
      , dwidth (true)
      , px (2 * priFont.getPx ())
      , py (priFont.getPy ())
   {
      load ();
   }

   // private methods

   bool Font::isLoadableChar (FT_ULong c)
   {
      if (c == Missing_Glyph_Marker)
         return true;

      if (c == Unicode_Replacement_Character)
         return true;

      return ((dwidth && wcwidth (c) == 2) ||
              (!dwidth && wcwidth (c) < 2));
   }

   void Font::load ()
   {
      FT_Library ft;
      FT_Face face;

      if (FT_Init_FreeType (&ft))
         throw std::runtime_error ("Could not initialize FreeType library");
      std::string filename = locateFontFile(font.get());
      logI << "Loading " << filename << " as "
           << (overlay ? "overlay" : (dwidth ? "double-width" : "primary"))
           << std::endl;
      if (FT_New_Face (ft, filename.c_str (), 0, &face))
         throw std::runtime_error (std::string ("Failed to load font ") +
                                   filename);

      double ptSize;
      double dpi;
      int pixelSize, antialias, hinting, hint_style, rgba, lcd_filter, autohint;

      if (FcPatternGetDouble(font.get(), FC_SIZE, 0, &ptSize) == FcResultMatch)
      {
         FcPatternGetDouble(font.get(), FC_DPI, 0, &dpi);
         pixelSize = (int)(ptSize * dpi / 72);
         logI << "Font size " << ptSize << " @ " << dpi << " DPI" << std::endl;
      }
      else
      {
         FcPatternGetInteger(font.get(), FC_PIXEL_SIZE, 0, &pixelSize);
      }
      if (FcPatternGetBool(font.get(), FC_ANTIALIAS, 0, &antialias) != FcResultMatch)
      {
         antialias = FcTrue;
      }
      if (FcPatternGetBool(font.get(), FC_HINTING, 0, &hinting) != FcResultMatch)
      {
         hinting = FcTrue;
      }
      if (FcPatternGetInteger(font.get(), FC_HINT_STYLE, 0, &hint_style) != FcResultMatch)
      {
         hint_style = FC_HINT_FULL;
      }
      if (FcPatternGetInteger(font.get(), FC_RGBA, 0, &rgba) != FcResultMatch)
      {
         rgba = FC_RGBA_UNKNOWN;
      }
      if (FcPatternGetInteger(font.get(), FC_LCD_FILTER, 0, &lcd_filter) != FcResultMatch)
      {
         lcd_filter = FC_LCD_DEFAULT;
      }
      if (FcPatternGetBool(font.get(), FC_AUTOHINT, 0, &autohint) != FcResultMatch)
      {
         autohint = FcFalse;
      }
      if (autohint)
      {
         glyph_load_flags |= FT_LOAD_FORCE_AUTOHINT;
      }
      if (!hinting || hint_style == FC_HINT_NONE)
      {
         glyph_load_flags |= FT_LOAD_NO_HINTING;
      }
      if (antialias)
      {
         if (FC_HINT_NONE < hint_style && hint_style < FC_HINT_FULL)
         {
            switch (rgba)
            {
               case FC_RGBA_RGB:
               case FC_RGBA_BGR:
                  glyph_load_flags |= FT_LOAD_TARGET_LIGHT;
                  glyph_render_mode = FT_RENDER_MODE_LCD;
                  break;
               default:
                  glyph_load_flags |= FT_LOAD_TARGET_LIGHT;
                  glyph_render_mode = FT_RENDER_MODE_LIGHT;
            }
         }
         else
         {
            switch (rgba)
            {
               case FC_RGBA_RGB:
               case FC_RGBA_BGR:
                  glyph_load_flags |= FT_LOAD_TARGET_LCD;
                  glyph_render_mode = FT_RENDER_MODE_LCD;
                  break;
               default:
                  glyph_load_flags |= FT_LOAD_TARGET_NORMAL;
                  glyph_render_mode = FT_RENDER_MODE_NORMAL;
            }
         }
      }
      else
      {
         glyph_load_flags |= FT_LOAD_TARGET_MONO;
         glyph_render_mode = FT_RENDER_MODE_MONO;
      }

      /* Determine the number of glyphs to actually load, based on wcwidth ()
       * We need this number up front to compute the atlas geometry.
       */
      int num_glyphs = 0;
      {
         FT_UInt gindex;
         FT_ULong charcode = FT_Get_First_Char (face, &gindex);
         while (gindex != 0)
         {
            if (isLoadableChar (charcode))
               ++ num_glyphs;
            charcode = FT_Get_Next_Char (face, charcode, &gindex);
         }
      }

      logT << "Family: " << face->family_name
           << "; Style: " << face->style_name
           << "; Faces: " << face->num_faces
           << "; Glyphs: " << num_glyphs << " to load ("
           << face->num_glyphs << " total)"
           << std::endl;

      if (face->num_fixed_sizes > 0)
         loadFixed (face, pixelSize);
      else
         loadScaled (face, pixelSize);

      FT_Library_SetLcdFilter(ft, (FT_LcdFilter)lcd_filter);

      /* Given that we have num_glyphs glyphs to load, with each
       * individual glyph having a size of px * py, compute nx and ny so
       * that the resulting atlas texture geometry is closest to a square.
       * We use one extra glyph space to guarantee a blank glyph at (0,0).
       */
      if (!overlay)
      {
         unsigned n_glyphs = num_glyphs + 1;
         unsigned long total_pixels = n_glyphs * px * py;
         double side = sqrt (total_pixels);
         nx = side / px;
         ny = side / py;
         while ((unsigned) nx * ny < n_glyphs)
         {
            if (px * nx < py * ny)
               ++nx;
            else
               ++ny;
         }

         if (nx > 255 || ny > 255)
         {
            logE << "Atlas geometry not addressable by single byte coords. "
                 << "Please report this as a bug with your font attached!"
                 << std::endl;
            throw std::runtime_error ("Impossible atlas geometry");
         }

         logT << "Atlas texture geometry: " << nx << "x" << ny
              << " glyphs of " << px << "x" << py << " each, "
              << "yielding pixel size " << nx*px << "x" << ny*py << "."
              << std::endl;
         logT << "Atlas holds space for " << nx*ny << " glyphs, "
              << n_glyphs << " will be used, empty: "
              << nx*ny - n_glyphs << " ("
              << 100.0 * (nx*ny - n_glyphs) / (nx*ny)
              << "%)" << std::endl;

         size_t atlas_bytes = bytes_per_pixel * nx * px * ny * py;
         logT << "Allocating " << atlas_bytes << " bytes for atlas buffer"
              << std::endl;
         atlasBuf.resize (atlas_bytes, 0);
      }

      FT_UInt gindex;
      FT_ULong charcode = FT_Get_First_Char (face, &gindex);
      while (gindex != 0)
      {
         if (isLoadableChar (charcode))
         {
            if (overlay)
            {
               const auto& it = atlasMap.find (charcode);
               if (it != atlasMap.end ())
               {
                  loadFace (face, charcode, it->second);
               }
            }
            else
            {
               loadFace (face, charcode);
            }
         }
         charcode = FT_Get_Next_Char (face, charcode, &gindex);
      }

      if (loadSkipCount)
      {
         logI << "Skipped loading " << loadSkipCount << " code point(s) "
              << "outside the Basic Multilingual Plane"
              << std::endl;
      }

      FT_Done_Face (face);
      FT_Done_FreeType (ft);
   }

   void Font::loadFixed (const FT_Face& face, int pixelSize)
   {
      int bestIdx = -1;
      int bestHeightDiff = std::numeric_limits<int>::max ();
      {
         std::ostringstream oss;
         oss << "Available sizes:";
         for (int i = 0; i < face->num_fixed_sizes; ++i)
         {
            oss << " " << face->available_sizes[i].width
                << "x" << face->available_sizes[i].height;

            int diff = abs (pixelSize - face->available_sizes[i].height);
            if (diff < bestHeightDiff)
            {
               bestIdx = i;
               bestHeightDiff = diff;
            }
         }
         logT << oss.str () << std::endl;
      }

      logT << "Configured size: " << (int)pixelSize
           << "; Best matching fixed size: "
           << face->available_sizes[bestIdx].width
           << "x" << face->available_sizes[bestIdx].height
           << std::endl;

      if (bestHeightDiff > 1 && face->units_per_EM > 0)
      {
         logT << "Size mismatch too large, fallback to rendering outlines."
              << std::endl;
         loadScaled (face, pixelSize);
         return;
      }

      const auto& facesize = face->available_sizes [bestIdx];

      if (overlay || dwidth)
      {
         if (px != facesize.width)
            throw std::runtime_error (
               "Overlay font size mismatch, expected px=" + std::to_string (px)
               + ", got: " + std::to_string (facesize.width));
         if (py != facesize.height)
            throw std::runtime_error (
               "Overlay font size mismatch, expected py=" + std::to_string (py)
               + ", got: " + std::to_string (facesize.height));
      }
      else
      {
         px = facesize.width;
         py = facesize.height;
         baseline = 0;
      }
      logI << "Glyph size " << px << "x" << py << std::endl;

      if (FT_Set_Pixel_Sizes (face, 0, py))
         throw std::runtime_error ("Could not set pixel sizes");

      if (!overlay && face->height)
      {
         // If we are loading a fixed bitmap strike of an otherwise scaled
         // font, we need the baseline metric.
         baseline = py * (double)face->ascender / face->height;
      }
   }

   void Font::loadScaled (const FT_Face& face, int pixelSize)
   {
      int tpx = pixelSize *
         (double)face->max_advance_width / face->units_per_EM;
      int tpy = tpx * (double)face->height / face->max_advance_width + 1;
      if (!overlay && !dwidth)
      {
         px = tpx;
         py = tpy;
      }
      if (!overlay)
      {
         baseline = tpy * (double)face->ascender / face->height;
      }
      logI << "Glyph size " << px << "x" << py << std::endl;
      if (FT_Set_Pixel_Sizes (face, 0, pixelSize))
         throw std::runtime_error ("Could not set pixel sizes");
   }

   void Font::loadFace (const FT_Face& face, FT_ULong c)
   {
      const uint8_t atlas_row = atlas_seq / nx;
      const uint8_t atlas_col = atlas_seq - nx * atlas_row;
      const AtlasPos apos = {atlas_col, atlas_row};

      loadFace (face, c, apos);
      atlasMap [c] = apos;
      ++atlas_seq;
   }

   void Font::loadFace (const FT_Face& face, FT_ULong c, const AtlasPos& apos)
   {
      if (c > std::numeric_limits<uint16_t>::max ())
      {
        #ifdef DEBUG
         logT << "Skip loading code point 0x" << std::hex << c << std::dec
              << " outside the Basic Multilingual Plane" << std::endl;
        #endif
         ++loadSkipCount;
         return;
      }

      if (FT_Load_Char (face, c, glyph_load_flags))
      {
         logW << "Failed to load glyph for char " << c << std::endl;
         return;
      }
      if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP)
      {
         if (FT_Render_Glyph (face->glyph, glyph_render_mode))
         {
            logW << "Failed to render glyph for char " << c << std::endl;
            return;
         }
      }

      // destination pixel offset
      const unsigned int xskip = face->glyph->bitmap_left < 0
                              ? -face->glyph->bitmap_left : 0; 
      const unsigned int dx = face->glyph->bitmap_left > 0
                            ? face->glyph->bitmap_left : 0;
      const unsigned int dy = baseline && baseline > face->glyph->bitmap_top
                            ? baseline - face->glyph->bitmap_top : 0;

      // raw/rasterized bitmap dimensions
      const unsigned int tw = glyph_render_mode == FT_RENDER_MODE_LCD ? face->glyph->bitmap.width / 3 : face->glyph->bitmap.width;
      const unsigned int bh = std::min (face->glyph->bitmap.rows, py - dy);
      const unsigned int bw = std::min (tw, px - dx);

      const int atlas_row_offset = bytes_per_pixel * nx * px * py;
      const int atlas_glyph_offset = apos.y * atlas_row_offset + bytes_per_pixel * apos.x * px;
      const int atlas_write_offset = atlas_glyph_offset + bytes_per_pixel * (nx * px * dy + dx);

      if (overlay) // clear glyph area, as we are overwriting an existing glyph
      {
         for (unsigned int j = 0; j < bh; ++j) {
            uint8_t* atl_dst_row =
               atlasBuf.data () + atlas_glyph_offset + bytes_per_pixel * j * nx * px;
            for (unsigned int k = 0; k < bw; ++k) {
               *atl_dst_row++ = 0;
               *atl_dst_row++ = 0;
               *atl_dst_row++ = 0;
               *atl_dst_row++ = 0;
            }
         }
      }

      /* Load bitmap into atlas buffer area. Each row in the bitmap
       * occupies bitmap.pitch bytes (with padding); this is the
       * increment in the input bitmap array per row.
       *
       * Interpretation of bytes within the bitmap rows is subject to
       * bitmap.pixel_mode, essentially either 8 bits (256-scale gray)
       * per pixel, or 1 bit (mono) per pixel. Leftmost pixel is MSB.
       *
       */
      const auto& bmp = face->glyph->bitmap;
      const uint8_t* bmp_src_row;
      uint8_t* atl_dst_row;
      switch (bmp.pixel_mode)
      {
      case FT_PIXEL_MODE_MONO:
         for (unsigned int j = 0; j < bh; ++j) {
            bmp_src_row = bmp.buffer + j * bmp.pitch + xskip;
            atl_dst_row = atlasBuf.data () + atlas_write_offset + bytes_per_pixel * j * nx * px;
            uint8_t byte = 0;
            int bl = face->glyph->bitmap_left;
            for (unsigned int k = 0; k < bw; ++k) {
               if (k % 8 == 0) {
                  byte = *bmp_src_row++;
               }
               uint8_t val = (byte & 0x80) ? 0xFF : 0;
               *atl_dst_row++ = val;
               *atl_dst_row++ = val;
               *atl_dst_row++ = val;
               atl_dst_row++;
               byte <<= 1;
            }
         }
         break;
      case FT_PIXEL_MODE_GRAY:
         for (unsigned int j = 0; j < bh; ++j) {
            bmp_src_row = bmp.buffer + j * bmp.pitch + xskip;
            atl_dst_row = atlasBuf.data () + atlas_write_offset + bytes_per_pixel * j * nx * px;
            for (unsigned int k = 0; k < bw; ++k) {
               uint8_t val = *bmp_src_row++;
               *atl_dst_row++ = val;
               *atl_dst_row++ = val;
               *atl_dst_row++ = val;
               atl_dst_row++;
            }
         }
         break;
      case FT_PIXEL_MODE_LCD:
         for (unsigned int j = 0; j < bh; ++j) {
            bmp_src_row = bmp.buffer + j * bmp.pitch + 3 * xskip;
            atl_dst_row = atlasBuf.data () + atlas_write_offset + bytes_per_pixel * j * nx * px;
            for (unsigned int k = 0; k < bw; k++) {
               *atl_dst_row++ = *bmp_src_row++;
               *atl_dst_row++ = *bmp_src_row++;
               *atl_dst_row++ = *bmp_src_row++;
               atl_dst_row++;
            }
         }
         break;
      default:
         throw std::runtime_error (
            std::string ("Unhandled pixel_type=") +
            std::to_string (bmp.pixel_mode));
      }
   }

} // namespace zutty
