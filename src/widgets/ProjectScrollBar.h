/*  SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

class wxWindow;

// The slice of wxScrollBar that ProjectWindow relies on, so the native wx
// scrollbar and the opt-in Alicorn-painted scrollbar are interchangeable.
class ProjectScrollBar {
public:
   virtual ~ProjectScrollBar() = default;
   virtual wxWindow *Window() = 0;
   virtual int GetThumbPosition() const = 0;
   virtual int GetThumbSize() const = 0;
   virtual int GetRange() const = 0;
   virtual int GetPageSize() const = 0;
   virtual void SetThumbPosition(int position) = 0;
   virtual void SetScrollbar(
      int position, int thumbSize, int range, int pageSize, bool refresh) = 0;
};
