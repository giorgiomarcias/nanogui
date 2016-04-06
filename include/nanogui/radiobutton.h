/*
    nanogui/radiobutton.h -- Radio+toggle button with an icon when cheked,
                             and another icon when unchecked.

    NanoGUI was developed by Wenzel Jakob <wenzel@inf.ethz.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#pragma once

#include <nanogui/button.h>
#include <nanogui/entypo.h>

NAMESPACE_BEGIN(nanogui)

class RadioButton : public Button {
public:
    RadioButton(Widget *parent, int checkedIcon = ENTYPO_ICON_CHECK, int uncheckdIcon = 0,
           const std::string &caption = "")
        : Button(parent, caption, uncheckdIcon), mOtherIcon(checkedIcon) {
        setFlags(Flags::RadioButton | Flags::ToggleButton);
        setFixedSize(Vector2i(25, 25));
    }

    void toggle() {
        if (mButtonGroup.empty()) {
            for (auto widget : parent()->children()) {
                Button *b = dynamic_cast<Button *>(widget);
                if (b && b != this && (b->flags() & Flags::RadioButton) && b->pushed()) {
                    RadioButton *rb = dynamic_cast<RadioButton *>(b);
                    if (rb)
                        std::swap(rb->mIcon, rb->mOtherIcon);
                }
            }
        } else {
            for (auto b : mButtonGroup) {
                if (b != this && (b->flags() & Flags::RadioButton) && b->pushed()) {
                    RadioButton *rb = dynamic_cast<RadioButton *>(b);
                    if (rb)
                        std::swap(rb->mIcon, rb->mOtherIcon);
                }
            }
        }
        std::swap(mIcon, mOtherIcon);
        Button::toggle();
    }

protected:
    int mOtherIcon;
};

NAMESPACE_END(nanogui)
