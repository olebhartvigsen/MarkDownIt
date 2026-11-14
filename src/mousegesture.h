#pragma once

// A press that the window answers on its own owns the whole mouse
// sequence.
//
// Two presses are answered before the document sees them: a card on the
// welcome screen, which opens the document it names, and a modified
// click on a link. Both platforms keep delivering the movement and the
// release of that press to the same view afterwards, and the document
// that the press just opened must not read them: a hand that moves a
// couple of points while clicking would otherwise leave a selection in
// the freshly opened page, anchored at its start.
//
// The shell marks the press consumed; its move and release handlers ask.
// The rule is the same on Windows and macOS, so it lives here and both
// shells hold one of these.
class MouseGesture {
public:
    // A new press starts a new sequence, whatever the last one did.
    void OnPress() { consumed_ = false; }

    // The press was answered by the window itself; the rest of the
    // sequence belongs to it.
    void Consume() { consumed_ = true; }

    // True while the movement and the release belong to a consumed press.
    bool Consumed() const { return consumed_; }

    // The release ends the sequence.
    void OnRelease() { consumed_ = false; }

private:
    bool consumed_ = false;
};
