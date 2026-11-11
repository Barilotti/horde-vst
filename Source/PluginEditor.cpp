#include "PluginEditor.h"

using namespace horde::ui;

namespace
{
juce::String uiSans()
{
   #if JUCE_WINDOWS
    return "Segoe UI";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

juce::Font uiFont (float h, bool bold = false, float kerning = 0.0f)
{
    return juce::Font (juce::FontOptions (uiSans(), h, bold ? juce::Font::bold : juce::Font::plain)
                           .withKerningFactor (kerning));
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    const auto cx = r.getCentreX(), cy = r.getCentreY(), s = r.getWidth();
    switch (icon)
    {
        case Icon::crowd:
        {
            g.fillEllipse (cx - s * 0.13f, cy - s * 0.42f, s * 0.26f, s * 0.26f);
            g.fillEllipse (cx - s * 0.45f, cy - s * 0.30f, s * 0.2f, s * 0.2f);
            g.fillEllipse (cx + s * 0.25f, cy - s * 0.30f, s * 0.2f, s * 0.2f);
            juce::Path p;
            p.addRoundedRectangle (cx - s * 0.22f, cy - s * 0.1f, s * 0.44f, s * 0.42f, s * 0.15f);
            p.addRoundedRectangle (cx - s * 0.52f, cy - s * 0.04f, s * 0.3f, s * 0.32f, s * 0.12f);
            p.addRoundedRectangle (cx + s * 0.22f, cy - s * 0.04f, s * 0.3f, s * 0.32f, s * 0.12f);
            g.fillPath (p);
            break;
        }
        case Icon::clock:
        {
            g.drawEllipse (r.reduced (s * 0.08f), 1.6f);
            g.drawLine (cx, cy, cx, cy - s * 0.28f, 1.6f);
            g.drawLine (cx, cy, cx + s * 0.2f, cy + s * 0.1f, 1.6f);
            break;
        }
        case Icon::wave:
        {
            juce::Path p;
            p.startNewSubPath (r.getX(), cy);
            const float xs[] = { 0.15f, 0.28f, 0.42f, 0.55f, 0.68f, 0.8f, 1.0f };
            const float ys[] = { 0.0f, -0.4f, 0.45f, -0.3f, 0.25f, 0.0f, 0.0f };
            for (int i = 0; i < 7; ++i)
                p.lineTo (r.getX() + xs[i] * s, cy + ys[i] * s);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Icon::output:
            g.drawEllipse (r.reduced (s * 0.08f), 1.6f);
            break;
        case Icon::room:
        {
            juce::Path p;
            p.startNewSubPath (r.getX() + s * 0.15f, r.getBottom());
            p.lineTo (r.getX() + s * 0.15f, cy);
            p.quadraticTo (cx, r.getY() - s * 0.1f, r.getRight() - s * 0.15f, cy);
            p.lineTo (r.getRight() - s * 0.15f, r.getBottom());
            g.strokePath (p, juce::PathStrokeType (1.6f));
            break;
        }
    }
}

void fillPanel (juce::Graphics& g, juce::Rectangle<float> b, float radius = 10.0f)
{
    // drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (b.translated (0, 3), radius);
    juce::ColourGradient grad (Colours::panelHi, b.getX(), b.getY(), Colours::panel, b.getX(), b.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, radius);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + radius, b.getRight() - radius);
}

// Head-and-shoulders listener silhouette with headphones, origin at bottom centre.
juce::Path listenerPath (float k)
{
    juce::Path body;
    body.startNewSubPath (-62 * k, 0);
    body.cubicTo (-62 * k, -34 * k, -44 * k, -46 * k, -17 * k, -50 * k);
    body.lineTo (-12 * k, -60 * k);
    body.lineTo (12 * k, -60 * k);
    body.lineTo (17 * k, -50 * k);
    body.cubicTo (44 * k, -46 * k, 62 * k, -34 * k, 62 * k, 0);
    body.closeSubPath();
    body.addEllipse (-19 * k, -104 * k, 38 * k, 48 * k);
    // ear cups
    body.addRoundedRectangle (-27 * k, -88 * k, 10 * k, 22 * k, 4 * k);
    body.addRoundedRectangle (17 * k, -88 * k, 10 * k, 22 * k, 4 * k);
    return body;
}
} // namespace

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypefaceName (uiSans());
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, Colours::panel);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::highlightColourId, Colours::blood);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::ember);
    setColour (juce::CaretComponent::caretColourId, Colours::ember);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float a0, float a1, juce::Slider& s)
{
    auto b = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (size, size);
    const auto c = b.getCentre();
    const float r = size * 0.5f;
    const float angle = a0 + pos * (a1 - a0);
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const float ringR = r - 4.0f;
    const float stroke = juce::jmax (3.0f, size * 0.055f);

    // outer glow when hovered/dragged
    if (s.isMouseOverOrDragging())
    {
        g.setColour (Colours::ember.withAlpha (0.08f));
        g.fillEllipse (b);
    }

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, ringR, ringR, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff2b1f1d));
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc with glow
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0, juce::jmin (from, angle), juce::jmax (from, angle), true);
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Colours::ember.withAlpha (0.07f));
            g.strokePath (arc, juce::PathStrokeType (stroke + (float) i * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::ColourGradient grad (Colours::blood, c.x - r, c.y + r, juce::Colour (0xffff7a3a), c.x + r, c.y - r, false);
        g.setGradientFill (grad);
        g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body: shadow, bevel, face
    const float br = ringR - stroke * 1.6f;
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (c.x - br, c.y - br + 3.0f, br * 2, br * 2);
    juce::ColourGradient bevel (juce::Colour (0xff4a403c), c.x, c.y - br, juce::Colour (0xff0e0b0b), c.x, c.y + br, false);
    g.setGradientFill (bevel);
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    const float fr = br * 0.86f;
    juce::ColourGradient face (juce::Colour (0xff2c2523), c.x - fr * 0.4f, c.y - fr * 0.6f,
                               juce::Colour (0xff121010), c.x + fr * 0.5f, c.y + fr, true);
    g.setGradientFill (face);
    g.fillEllipse (c.x - fr, c.y - fr, fr * 2, fr * 2);

    // pointer
    juce::Path p;
    const float pw = juce::jmax (2.0f, size * 0.035f);
    p.addRoundedRectangle (-pw * 0.5f, -fr + 2.0f, pw, fr * 0.42f, pw * 0.5f);
    g.setColour (Colours::ember.withAlpha (0.35f));
    g.fillPath (p, juce::AffineTransform::scale (1.8f, 1.0f).rotated (angle).translated (c));
    g.setColour (juce::Colour (0xffffb08a));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
    juce::ColourGradient grad (Colours::male.withAlpha (0.8f), track.getX(), 0, Colours::female.withAlpha (0.8f), track.getRight(), 0, false);
    grad.addColour (0.5, juce::Colour (0xff3a2c2a));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (Colours::textDim.withAlpha (0.6f));
    g.fillRect (juce::Rectangle<float> (track.getCentreX() - 0.5f, track.getY() - 4, 1.0f, 12.0f));

    const float tx = pos;
    auto thumb = juce::Rectangle<float> (10.0f, 18.0f).withCentre ({ tx, r.getCentreY() });
    g.setColour (Colours::ember.withAlpha (0.25f));
    g.fillRoundedRectangle (thumb.expanded (4.0f), 5.0f);
    juce::ColourGradient tg (juce::Colour (0xffff6a3a), thumb.getX(), thumb.getY(), Colours::blood, thumb.getX(), thumb.getBottom(), false);
    g.setGradientFill (tg);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool small = b.getWidth() < 40;
    juce::ColourGradient grad (juce::Colour (0xff221a19).brighter (over ? 0.15f : 0.0f), r.getX(), r.getY(),
                               juce::Colour (0xff120e0e), r.getX(), r.getBottom(), false);
    if (down) grad = juce::ColourGradient (Colours::blood.darker (0.4f), r.getX(), r.getY(), Colours::panel, r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour ((small ? Colours::panelEdge : Colours::red.withAlpha (over ? 0.9f : 0.55f)));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool isReroll = b.getButtonText() == "REROLL";
    if (isReroll)
    {
        // circular-arrow icon
        auto ic = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getX() + 24.0f, r.getCentreY() });
        juce::Path arc;
        arc.addCentredArc (ic.getCentreX(), ic.getCentreY(), 6.5f, 6.5f, 0, -2.4f, 2.2f, true);
        g.setColour (Colours::red.brighter (over ? 0.3f : 0.0f));
        g.strokePath (arc, juce::PathStrokeType (1.8f));
        juce::Path head;
        const float ax = ic.getCentreX() + 6.5f * std::sin (-2.4f), ay = ic.getCentreY() - 6.5f * std::cos (-2.4f);
        head.addTriangle (ax - 4.0f, ay - 1.0f, ax + 2.0f, ay - 4.5f, ax + 1.5f, ay + 2.5f);
        g.fillPath (head);
        g.setFont (uiFont (14.0f, true, 0.08f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (36.0f), juce::Justification::centredLeft);
        return;
    }
    g.setColour (over ? Colours::text : Colours::textDim);
    g.setFont (uiFont (14.0f, true));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
void Logo::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    juce::GlyphArrangement ga;
   #if JUCE_WINDOWS
    juce::Font f (juce::FontOptions ("Impact", 62.0f, juce::Font::plain).withKerningFactor (0.02f));
   #else
    juce::Font f (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 60.0f, juce::Font::bold).withKerningFactor (-0.02f));
   #endif
    ga.addLineOfText (f, "HORDE", 0.0f, 0.0f);
    juce::Path text;
    ga.createPath (text);
    auto tb = text.getBounds();
    // brush-italic shear and fit
    const float targetH = r.getHeight() * 0.56f;
    const float sc = targetH / tb.getHeight();
    auto xf = juce::AffineTransform::translation (-tb.getX(), -tb.getY())
                  .scaled (sc)
                  .followedBy (juce::AffineTransform (1.0f, -0.22f, 0.0f, 0.0f, 1.0f, 0.0f))
                  .translated (r.getX() + 34.0f, r.getY() + 8.0f);
    text.applyTransform (xf);
    auto bb = text.getBounds();

    // red haze behind
    g.setColour (Colours::red.withAlpha (0.12f));
    g.fillEllipse (bb.expanded (20.0f, 4.0f));

    // brush tails (left & right), tapered strokes
    juce::Random rnd (7);
    for (int i = 0; i < 6; ++i)
    {
        const float y = bb.getY() + bb.getHeight() * (0.25f + 0.55f * rnd.nextFloat());
        const bool left = i % 2 == 0;
        const float len = 20.0f + 45.0f * rnd.nextFloat();
        juce::Path p;
        const float x0 = left ? bb.getX() + 8.0f : bb.getRight() - 10.0f;
        const float x1 = left ? x0 - len : x0 + len;
        const float th = 0.6f + 1.4f * rnd.nextFloat();
        p.startNewSubPath (x0, y - th);
        p.quadraticTo ((x0 + x1) * 0.5f, y - th * 0.5f - 2.0f, x1, y + 1.0f);
        p.quadraticTo ((x0 + x1) * 0.5f, y + th * 0.5f, x0, y + th);
        p.closeSubPath();
        g.setColour (Colours::red.withAlpha (0.25f + 0.3f * rnd.nextFloat()));
        g.fillPath (p);
    }

    // lettering
    juce::ColourGradient grad (juce::Colour (0xffff4a36), bb.getX(), bb.getY(), juce::Colour (0xff8e0f0c), bb.getX(), bb.getBottom(), false);
    grad.addColour (0.45, juce::Colour (0xffe0261f));
    g.setGradientFill (grad);
    g.fillPath (text);

    // distress: dark scratches + specks inside the letters
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (text);
        juce::Random rr (42);
        g.setColour (Colours::bg.withAlpha (0.85f));
        for (int i = 0; i < 26; ++i)
        {
            const float y = bb.getY() + bb.getHeight() * rr.nextFloat();
            const float x = bb.getX() + bb.getWidth() * rr.nextFloat();
            g.drawLine (x, y, x + 8.0f + 30.0f * rr.nextFloat(), y - 2.0f + 4.0f * rr.nextFloat(), 0.6f + 1.2f * rr.nextFloat());
        }
        for (int i = 0; i < 70; ++i)
        {
            const float d = 0.8f + 1.6f * rr.nextFloat();
            g.fillEllipse (bb.getX() + bb.getWidth() * rr.nextFloat(), bb.getY() + bb.getHeight() * rr.nextFloat(), d, d);
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (bb.withHeight (bb.getHeight() * 0.18f));
    }

    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.0f, true, 0.32f));
    g.drawText ("VOCAL CROWD GENERATOR", juce::Rectangle<float> (bb.getX() + 40.0f, bb.getBottom() + 3.0f, 300.0f, 14.0f),
                juce::Justification::left);
}

//==============================================================================
void Battlefield::tick()
{
    for (int i = 0; i < horde::kMaxVoices; ++i)
    {
        const auto& v = proc.getEngine().voice (i);
        const float lvl = juce::jlimit (0.0f, 1.0f, v.vizLevel.load() * 3.0f);
        smooth[(size_t) i] = juce::jmax (lvl, smooth[(size_t) i] * 0.85f);
        fade[(size_t) i] += ((v.vizOn.load() ? 1.0f : 0.0f) - fade[(size_t) i]) * 0.2f;
    }
    repaint();
}

void Battlefield::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // dome-shaped screen
    juce::Path shape;
    shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 14.0f, 14.0f, true, true, true, true);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (shape, juce::AffineTransform::translation (0, 3));
    juce::ColourGradient bgGrad (juce::Colour (0xff141011), r.getCentreX(), r.getY(),
                                 juce::Colour (0xff1c0d0c), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (bgGrad);
    g.fillPath (shape);

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (shape);

    const float cx = r.getCentreX();
    const float base = r.getBottom() - 6.0f;
    const float depth = r.getHeight() - 62.0f;
    const float halfW = r.getWidth() * 0.5f - 34.0f;

    // floor glow
    juce::ColourGradient glow (Colours::red.withAlpha (0.35f), cx, base, juce::Colours::transparentBlack, cx, base - depth * 0.75f, true);
    g.setGradientFill (glow);
    g.fillRect (r);

    // perspective grid: ellipse arcs around listener + radial lines
    for (int i = 1; i <= 6; ++i)
    {
        const float t = (float) i / 6.0f;
        const float ry = depth * t * 1.02f, rx = halfW * (0.35f + 0.75f * t);
        g.setColour (juce::Colour (0xff5a3a36).withAlpha (0.42f - 0.04f * (float) i));
        g.drawEllipse (cx - rx, base - ry, rx * 2, ry * 2, 1.0f);
    }
    for (int i = -4; i <= 4; ++i)
    {
        const float a = (float) i * 0.32f;
        g.setColour (juce::Colour (0xff5a3a36).withAlpha (0.22f));
        g.drawLine (cx, base, cx + std::sin (a) * r.getWidth(), base - std::cos (a) * r.getHeight() * 1.3f, 1.0f);
    }

    // voices
    const auto& eng = proc.getEngine();
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < horde::kMaxVoices; ++i)
        {
            const float f = fade[(size_t) i];
            if (f < 0.02f) continue;
            const auto& v = eng.voice (i);
            const float dist = v.vizDist.load();
            const float pan  = v.vizPan.load();
            const float yN   = 0.30f + 0.66f * dist;           // keep dots above the listener
            const float x = cx + pan * halfW * (0.55f + 0.45f * yN);
            const float y = base - yN * depth;
            const float lvl = smooth[(size_t) i];
            const auto col = v.vizFemale.load() ? Colours::female : Colours::male;
            const float rad = (2.6f + 4.8f * lvl) * (1.25f - 0.55f * dist);
            if (pass == 0)
            {
                juce::ColourGradient halo (col.withAlpha ((0.16f + 0.4f * lvl) * f), x, y, col.withAlpha (0.0f), x + rad * 4.0f, y, true);
                g.setGradientFill (halo);
                g.fillEllipse (x - rad * 4.0f, y - rad * 4.0f, rad * 8.0f, rad * 8.0f);
            }
            else
            {
                g.setColour (col.withAlpha ((0.55f + 0.45f * lvl) * f));
                g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
                g.setColour (juce::Colours::white.withAlpha (0.5f * lvl * f));
                g.fillEllipse (x - rad * 0.4f, y - rad * 0.4f, rad * 0.8f, rad * 0.8f);
            }
        }

    // listener
    {
        const float k = 0.85f;
        auto body = listenerPath (k);
        body.applyTransform (juce::AffineTransform::translation (cx, base + 2.0f));
        juce::ColourGradient halo (Colours::red.withAlpha (0.45f), cx, base - 40.0f, juce::Colours::transparentBlack, cx + 120.0f, base - 40.0f, true);
        g.setGradientFill (halo);
        g.fillEllipse (cx - 120.0f, base - 160.0f, 240.0f, 240.0f);
        juce::ColourGradient fill (juce::Colour (0xff231a19), cx, base - 100.0f, juce::Colour (0xff070505), cx, base, false);
        g.setGradientFill (fill);
        g.fillPath (body);
        g.setColour (Colours::red.withAlpha (0.55f));
        g.strokePath (body, juce::PathStrokeType (1.2f));
        juce::Path band;
        band.addCentredArc (cx, base + 2.0f - 80.0f * k, 23.0f * k, 26.0f * k, 0, -1.75f, 1.75f, true);
        g.setColour (juce::Colour (0xff0a0707));
        g.strokePath (band, juce::PathStrokeType (4.0f * k));
        g.setColour (Colours::red.withAlpha (0.5f));
        g.strokePath (band, juce::PathStrokeType (1.0f));
    }

    // labels
    g.setFont (uiFont (11.0f, true, 0.35f));
    g.setColour (Colours::textDim);
    auto top = r.withHeight (34.0f);
    g.drawText ("STEREO FIELD", top.withRight (cx - 12.0f), juce::Justification::centredRight);
    g.drawText ("TOP VIEW", top.withLeft (cx + 12.0f), juce::Justification::centredLeft);
    g.setColour (Colours::red);
    g.fillEllipse (cx - 2.5f, top.getCentreY() - 2.5f, 5.0f, 5.0f);
    {
        const bool files = proc.getEngine().filesMode();
        g.setFont (uiFont (10.5f, true, 0.2f));
        g.setColour (files ? Colours::ember : Colours::textDim);
        g.drawText (files ? "MODE: TAKE FILES" : "MODE: LIVE INPUT", juce::Rectangle<float> (r.getX() + 18, r.getY() + 10, 200, 14), juce::Justification::left);
    }
    g.setColour (Colours::textDim);
    g.setFont (uiFont (14.0f, true));
    g.drawText ("L", juce::Rectangle<float> (r.getX() + 12, r.getCentreY() - 40, 20, 20), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (r.getRight() - 32, r.getCentreY() - 40, 20, 20), juce::Justification::centred);

    auto leg = juce::Rectangle<float> (r.getX() + 20.0f, r.getBottom() - 30.0f, 200.0f, 18.0f);
    g.setFont (uiFont (12.5f));
    g.setColour (Colours::male);   g.fillEllipse (leg.getX(), leg.getCentreY() - 4.5f, 9, 9);
    g.setColour (Colours::text);   g.drawText ("Male", leg.withTrimmedLeft (15), juce::Justification::centredLeft);
    g.setColour (Colours::female); g.fillEllipse (leg.getX() + 66, leg.getCentreY() - 4.5f, 9, 9);
    g.setColour (Colours::text);   g.drawText ("Female", leg.withTrimmedLeft (81), juce::Justification::centredLeft);

}

//==============================================================================
void InputMeters::tick()
{
    for (int i = 0; i < horde::kMaxTakes; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (proc.getEngine().inputLevel (i), -60.0f);
        const float n = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        lv[(size_t) i] = juce::jmax (n, lv[(size_t) i] - 0.025f);
    }
    repaint();
}

static void drawSegmentBar (juce::Graphics& g, juce::Rectangle<float> bar, float level, bool enabled, int segments = 22)
{
    g.setColour (juce::Colour (0xff0d0909));
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (juce::Colour (0xff2a201e));
    g.drawRoundedRectangle (bar, 3.0f, 1.0f);
    auto inner = bar.reduced (3.0f);
    const float segH = inner.getHeight() / (float) segments;
    const int lit = juce::roundToInt (level * (float) segments);
    for (int s = 0; s < segments; ++s)
    {
        auto seg = juce::Rectangle<float> (inner.getX(), inner.getBottom() - segH * (float) (s + 1), inner.getWidth(), segH - 1.5f);
        const float t = (float) s / (float) (segments - 1);
        auto col = juce::Colour (0xff8a1410).interpolatedWith (juce::Colour (0xffff7a2e), t);
        if (enabled && s < lit)
        {
            g.setColour (col);
            g.fillRect (seg);
            g.setColour (col.withAlpha (0.25f));
            g.fillRect (seg.expanded (2.0f, 0.5f));
        }
        else
        {
            g.setColour (enabled ? juce::Colour (0xff221816) : juce::Colour (0xff171212));
            g.fillRect (seg);
        }
    }
}

void InputMeters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    fillPanel (g, r);

    const int takes = (int) proc.apvts.getRawParameterValue ("takes")->load();
    const int male  = (int) proc.apvts.getRawParameterValue ("maleTakes")->load();
    const int nIn   = juce::jmax (1, proc.getNumConnectedInputs());

    auto inner = r.reduced (16.0f, 12.0f);
    auto nums = inner.removeFromTop (22.0f);
    auto tags = inner.removeFromBottom (24.0f);
    inner.removeFromTop (4.0f);
    inner.removeFromBottom (4.0f);
    const float slotW = inner.getWidth() / (float) horde::kMaxTakes;
    for (int i = 0; i < horde::kMaxTakes; ++i)
    {
        const bool used = takes == 0 ? proc.getEngine().slotActive (i) : (i < takes && i < nIn);
        const bool fem  = i >= male;
        auto col = juce::Rectangle<float> (inner.getX() + slotW * (float) i, inner.getY(), slotW, inner.getHeight());
        drawSegmentBar (g, col.withSizeKeepingCentre (juce::jmin (20.0f, slotW - 10.0f), col.getHeight()), lv[(size_t) i], used);

        g.setFont (uiFont (15.0f, true));
        g.setColour (used ? Colours::text : Colours::textDim.withAlpha (0.5f));
        g.drawText (juce::String (i + 1), col.withY (nums.getY()).withHeight (nums.getHeight()), juce::Justification::centred);
        g.setFont (uiFont (14.0f, true));
        g.setColour (used ? Colours::red : Colours::textDim.withAlpha (0.4f));
        g.drawText (used ? (fem ? "F" : "M") : juce::String::fromUTF8 ("\xe2\x80\x93"),
                    col.withY (tags.getY()).withHeight (tags.getHeight()), juce::Justification::centred);
    }
}

//==============================================================================
void OutputMeter::tick()
{
    for (int i = 0; i < 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (proc.getEngine().outputLevel (i), -60.0f);
        const float n = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        lv[(size_t) i] = juce::jmax (n, lv[(size_t) i] - 0.025f);
    }
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float w = (r.getWidth() - 4.0f) * 0.5f;
    drawSegmentBar (g, r.withWidth (w), lv[0], true, 18);
    drawSegmentBar (g, r.withTrimmedLeft (w + 4.0f), lv[1], true, 18);
}

//==============================================================================
juce::Rectangle<float> TakesPanel::rowBounds (int visibleRow) const
{
    return { 14.0f, 44.0f + 23.0f * (float) visibleRow, (float) getWidth() - 28.0f, 21.0f };
}

void TakesPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    fillPanel (g, r);
    const auto& takes = proc.getTakes();

    g.setColour (Colours::red.brighter (0.2f));
    g.setFont (uiFont (13.0f, true, 0.06f));
    g.drawText ("TAKE FILES  (" + juce::String ((int) takes.size()) + ")", juce::Rectangle<float> (16, 10, 200, 22), juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (10.5f));
    g.drawText ("drop more anywhere", juce::Rectangle<float> (r.getWidth() - 136, 10, 122, 22), juce::Justification::centredRight);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (38, 14.0f, r.getWidth() - 14.0f);

    const int visible = (int) ((r.getHeight() - 52.0f) / 23.0f);
    scroll = juce::jlimit (0, juce::jmax (0, (int) takes.size() - visible), scroll);
    for (int row = 0; row < visible; ++row)
    {
        const int i = row + scroll;
        if (i >= (int) takes.size()) break;
        auto& t = *takes[(size_t) i];
        auto b = rowBounds (row);
        g.setColour (juce::Colour (0xff1d1716));
        g.fillRoundedRectangle (b, 4.0f);

        const bool fem = t.female.load();
        auto pill = juce::Rectangle<float> (b.getRight() - 52.0f, b.getY() + 3.0f, 24.0f, b.getHeight() - 6.0f);
        g.setColour ((fem ? Colours::female : Colours::male).withAlpha (0.85f));
        g.fillRoundedRectangle (pill, 7.0f);
        g.setColour (juce::Colours::black);
        g.setFont (uiFont (11.0f, true));
        g.drawText (fem ? "F" : "M", pill, juce::Justification::centred);

        g.setColour (Colours::textDim);
        g.setFont (uiFont (14.0f, true));
        g.drawText (juce::String::fromUTF8 ("\xc3\x97"), juce::Rectangle<float> (b.getRight() - 22.0f, b.getY(), 18.0f, b.getHeight()), juce::Justification::centred);

        g.setColour (Colours::textDim);
        g.setFont (uiFont (11.0f, true));
        g.drawText (juce::String (i + 1), b.withWidth (22.0f), juce::Justification::centred);
        g.setColour (Colours::text);
        g.setFont (uiFont (12.5f));
        g.drawFittedText (t.name, b.withTrimmedLeft (24.0f).withTrimmedRight (58.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    if ((int) takes.size() > visible)
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (10.0f));
        g.drawText (juce::String (scroll + 1) + "-" + juce::String (juce::jmin ((int) takes.size(), scroll + visible)) + " of " + juce::String ((int) takes.size()) + "  (scroll)",
                    juce::Rectangle<float> (14, r.getHeight() - 16, r.getWidth() - 28, 12), juce::Justification::centredRight);
    }
}

void TakesPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto& takes = proc.getTakes();
    for (int row = 0; row < 12; ++row)
    {
        auto b = rowBounds (row);
        if (! b.contains (e.position)) continue;
        const int i = row + scroll;
        if (i >= (int) takes.size()) return;
        if (e.position.x > b.getRight() - 26.0f)       proc.removeTake (i);
        else if (e.position.x > b.getRight() - 56.0f)  proc.toggleFemale (i);
        repaint();
        return;
    }
}

void TakesPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scroll += w.deltaY < 0 ? 1 : -1;
    repaint();
}

//==============================================================================
RoomView::RoomView (HordeProcessor& p) : proc (p)
{
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    addAndMakeVisible (prev);
    addAndMakeVisible (next);
}

void RoomView::step (int delta)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("roomType")))
    {
        const int n = param->choices.size();
        const int idx = (param->getIndex() + delta + n) % n;
        param->beginChangeGesture();
        *param = idx;
        param->endChangeGesture();
    }
}

void RoomView::tick()
{
    const int t = (int) proc.apvts.getRawParameterValue ("roomType")->load();
    if (t != lastType) { lastType = t; repaint(); }
}

void RoomView::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromBottom (44).reduced (14, 8);
    prev.setBounds (bar.removeFromLeft (28));
    next.setBounds (bar.removeFromRight (28));
}

void RoomView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    fillPanel (g, r);
    auto scene = r.reduced (8.0f).withTrimmedBottom (40.0f);
    const int type = juce::jlimit (0, 3, (int) proc.apvts.getRawParameterValue ("roomType")->load());

    juce::Path clip;
    clip.addRoundedRectangle (scene, 7.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    const float cx = scene.getCentreX();
    const float horizon = scene.getY() + scene.getHeight() * 0.52f;

    // backdrop
    juce::ColourGradient sky (juce::Colour (0xff0d0909), cx, scene.getY(), juce::Colour (0xff3a0f0c), cx, horizon, false);
    g.setGradientFill (sky);
    g.fillRect (scene);
    juce::ColourGradient floor (juce::Colour (0xff1a0b0a), cx, horizon, juce::Colour (0xff060404), cx, scene.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRect (scene.withTop (horizon));

    // central light
    juce::ColourGradient light (Colours::red.withAlpha (0.55f), cx, horizon - 6.0f, juce::Colours::transparentBlack, cx + scene.getWidth() * 0.45f, horizon, true);
    g.setGradientFill (light);
    g.fillRect (scene);

    const auto stone = juce::Colour (0xff0b0707);
    const auto rim   = Colours::red.withAlpha (0.35f);

    if (type == 0) // Field: rolling hills + banners
    {
        juce::Path hills;
        hills.startNewSubPath (scene.getX(), horizon + 6.0f);
        for (int i = 0; i <= 8; ++i)
        {
            const float x = scene.getX() + scene.getWidth() * (float) i / 8.0f;
            hills.quadraticTo (x - scene.getWidth() / 16.0f, horizon - 10.0f - (float) ((i * 37) % 13), x, horizon + 2.0f);
        }
        hills.lineTo (scene.getRight(), scene.getBottom());
        hills.lineTo (scene.getX(), scene.getBottom());
        hills.closeSubPath();
        g.setColour (stone); g.fillPath (hills);
        g.setColour (rim);   g.strokePath (hills, juce::PathStrokeType (1.0f));
        for (int i = 0; i < 7; ++i)
        {
            const float x = scene.getX() + 20.0f + (scene.getWidth() - 40.0f) * (float) i / 6.0f;
            g.setColour (stone.brighter (0.1f));
            g.drawLine (x, horizon - 2.0f, x, horizon - 30.0f - (float) ((i * 7) % 10), 1.2f);
            juce::Path flag;
            flag.addTriangle (x, horizon - 30.0f, x + 10.0f, horizon - 26.0f, x, horizon - 22.0f);
            g.setColour (Colours::blood); g.fillPath (flag);
        }
    }
    else
    {
        // receding arches on both sides (hall/cathedral) or walls (courtyard)
        const float vpY = horizon - 4.0f;
        const int n = 5;
        for (int side = -1; side <= 1; side += 2)
            for (int i = n - 1; i >= 0; --i)
            {
                const float t  = (float) i / (float) n;                 // 0 near, 1 far
                const float sc = 1.0f - 0.8f * t;
                const float x  = cx + (float) side * (scene.getWidth() * 0.5f * sc + 8.0f) * 0.92f;
                const float colW = 12.0f * sc;
                const float h  = (type == 3 ? 150.0f : type == 2 ? 110.0f : 60.0f) * sc;
                const float bottom = vpY + (scene.getBottom() - vpY) * sc;
                juce::Path pillar;
                pillar.addRectangle (x - colW * 0.5f, bottom - h, colW, h);
                if (type == 1)
                    for (int m = 0; m < 2; ++m)  // battlements
                        pillar.addRectangle (x - colW * 0.5f + (float) m * colW * 0.6f, bottom - h - 5.0f * sc, colW * 0.4f, 5.0f * sc);
                g.setColour (stone); g.fillPath (pillar);
                g.setColour (rim.withAlpha (0.25f + 0.25f * (1.0f - t))); g.strokePath (pillar, juce::PathStrokeType (1.0f));

                if (type >= 2 && i > 0)
                {
                    // arch connecting to the next-nearer pillar
                    const float t2 = (float) (i - 1) / (float) n, sc2 = 1.0f - 0.8f * t2;
                    const float x2 = cx + (float) side * (scene.getWidth() * 0.5f * sc2 + 8.0f) * 0.92f;
                    const float h2 = (type == 3 ? 150.0f : 110.0f) * sc2;
                    const float b2 = vpY + (scene.getBottom() - vpY) * sc2;
                    juce::Path arch;
                    const float yA = b2 - h2, yB = bottom - h;
                    const float peak = juce::jmin (yA, yB) - (type == 3 ? 28.0f : 14.0f) * sc2;
                    arch.startNewSubPath (x2, yA);
                    if (type == 3) { arch.lineTo ((x + x2) * 0.5f, peak); arch.lineTo (x, yB); }
                    else           arch.quadraticTo ((x + x2) * 0.5f, peak, x, yB);
                    g.setColour (stone.brighter (0.05f));
                    g.strokePath (arch, juce::PathStrokeType (5.0f * sc2));
                    g.setColour (rim.withAlpha (0.3f));
                    g.strokePath (arch, juce::PathStrokeType (1.0f));
                }
            }

        // far doorway / window
        const float dw = type == 3 ? 26.0f : 20.0f, dh = type == 3 ? 60.0f : 40.0f;
        juce::Path door;
        door.startNewSubPath (cx - dw * 0.5f, vpY + 8.0f);
        door.lineTo (cx - dw * 0.5f, vpY + 8.0f - dh * 0.7f);
        if (type == 3) door.lineTo (cx, vpY + 8.0f - dh); else door.quadraticTo (cx, vpY + 8.0f - dh * 1.05f, cx + dw * 0.5f, vpY + 8.0f - dh * 0.7f);
        door.lineTo (cx + dw * 0.5f, vpY + 8.0f - dh * 0.7f);
        door.lineTo (cx + dw * 0.5f, vpY + 8.0f);
        door.closeSubPath();
        g.setColour (juce::Colour (0xffff6a40).withAlpha (0.85f));
        g.fillPath (door);
        if (type == 3)
        {
            g.setColour (juce::Colour (0xffff6a40).withAlpha (0.55f));
            g.fillEllipse (cx - 9.0f, vpY - dh - 22.0f, 18.0f, 18.0f);
        }
    }

    // crowd silhouettes in front
    juce::Random rr (1234);
    for (int row = 0; row < 3; ++row)
    {
        const float yb = scene.getBottom() + 6.0f - (float) (2 - row) * 9.0f;
        const float sz = 7.0f + 3.5f * (float) row;
        const auto shade = juce::Colour (0xff050303).brighter (0.04f * (float) (2 - row));
        for (float x = scene.getX() - 4.0f; x < scene.getRight() + 4.0f; x += sz * (1.4f + 0.6f * rr.nextFloat()))
        {
            const float hy = yb - sz * (2.4f + 0.5f * rr.nextFloat());
            juce::Path p;
            p.addEllipse (x - sz * 0.45f, hy, sz * 0.9f, sz * 1.05f);
            p.addRoundedRectangle (x - sz * 0.9f, hy + sz * 0.95f, sz * 1.8f, sz * 3.0f, sz * 0.6f);
            g.setColour (shade);
            g.fillPath (p);
            g.setColour (Colours::red.withAlpha (0.18f));
            g.strokePath (p, juce::PathStrokeType (0.8f));
            if (rr.nextFloat() < 0.18f) // raised weapon
            {
                g.setColour (shade.brighter (0.1f));
                g.drawLine (x + sz * 0.6f, hy + sz, x + sz * 1.2f, hy - sz * 2.2f, 1.3f);
            }
        }
    }
    g.restoreState();

    // label
    auto bar = r.removeFromBottom (44.0f).reduced (14.0f, 6.0f);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (10.0f, true, 0.3f));
    g.drawText ("ROOM", bar.removeFromTop (12.0f), juce::Justification::centred);
    g.setColour (Colours::text);
    g.setFont (uiFont (15.0f, true));
    static const char* names[] = { "Field", "Courtyard", "Hall", "Cathedral" };
    g.drawText (names[type], bar, juce::Justification::centred);
}

//==============================================================================
Content::Content (HordeProcessor& p)
    : proc (p), field (p), meters (p), takesPanel (p), outMeter (p), room (p)
{
    setLookAndFeel (&lnf);

    addKnob ("voices", "VOICES", "1 - 100").big = true;
    addKnob ("mix", "MIX", "files: phrase jumps").side = true;
    addKnob ("maleTakes", "MALE INPUTS", "live input mode").side = true;
    addKnob ("delayMin", "DELAY MIN");
    addKnob ("delaySpread", "DELAY SPREAD");
    addKnob ("pitchSpread", "PITCH SPREAD");
    addKnob ("drift", "DRIFT");
    addKnob ("timbre", "TIMBRE");
    addKnob ("humanize", "HUMANIZE");
    addKnob ("distance", "DISTANCE");
    addKnob ("depthSpread", "DEPTH SPREAD");
    addKnob ("width", "WIDTH");
    addKnob ("reverb", "REVERB");
    addKnob ("room", "ROOM SIZE");
    addKnob ("dry", "DRY");
    addKnob ("wet", "WET").slider.getProperties().set ("bipolar", true);
    addKnob ("scatter", "SCATTER");
    addKnob ("loose", "LOOSE");
    knobs.front()->side = true;

    balanceAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "balance", balance);
    balance.setDoubleClickReturnValue (true, 0.0);
    balance.onValueChange = [this] { repaint(); };
    addAndMakeVisible (balance);

    panels = { { "CROWD", Icon::crowd, { 12, 92, 222, 374 } },
               { "TIME & PITCH", Icon::clock, { 12, 476, 380, 214 } },
               { "COLOR & SPACE", Icon::wave, { 402, 476, 362, 214 } },
               { "OUTPUT", Icon::output, { 834, 314, 274, 152 } } };

    rerollButton.onClick = [this] { reroll(); };
    rerollButton.setTooltip ("Re-randomize every voice (delay, pitch, timbre, position)");
    addAndMakeVisible (rerollButton);

    seedValue.setEditable (false, true);
    seedValue.setJustificationType (juce::Justification::centredLeft);
    seedValue.setFont (uiFont (16.0f, true));
    seedValue.setTooltip ("Double-click to type a seed");
    seedValue.onTextChange = [this]
    {
        const int v = juce::jlimit (1, 9999, seedValue.getText().getIntValue());
        if (auto* param = proc.apvts.getParameter ("seed"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) v));
            param->endChangeGesture();
        }
    };
    addAndMakeVisible (seedValue);

    addAndMakeVisible (logo);
    addAndMakeVisible (field);
    addAndMakeVisible (meters);
    addChildComponent (takesPanel);

    addButton.onClick = [this] { chooseFiles(); };
    addButton.setTooltip ("Load take files (WAV/AIFF/FLAC/MP3). You can also drag them onto the plugin.");
    addAndMakeVisible (addButton);
    proc.onTakesChanged = [this] { updateMode(); takesPanel.repaint(); field.repaint(); };
    updateMode();
    addAndMakeVisible (outMeter);
    addAndMakeVisible (room);

    setSize (kDesignW, kDesignH);
    startTimerHz (30);
}

Content::~Content()
{
    proc.onTakesChanged = nullptr;
    stopTimer();
    setLookAndFeel (nullptr);
}

Knob& Content::addKnob (const char* id, const char* label, const char* range)
{
    auto k = std::make_unique<Knob>();
    k->id = id; k->label = label; k->range = range;
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    if (auto* param = proc.apvts.getParameter (id))
        k->slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    k->slider.setPopupDisplayEnabled (false, false, nullptr);
    k->slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void Content::updateMode()
{
    const bool files = ! proc.getTakes().empty();
    takesPanel.setVisible (files);
    meters.setVisible (! files);
}

void Content::chooseFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Load take files", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              juce::StringArray paths;
                              for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                              if (! paths.isEmpty()) proc.addFiles (paths);
                          });
}

bool Content::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void Content::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    proc.addFiles (files);
    repaint();
}

void Content::reroll()
{
    if (auto* param = proc.apvts.getParameter ("seed"))
    {
        const int cur = (int) proc.apvts.getRawParameterValue ("seed")->load();
        int next = cur;
        while (next == cur)
            next = 1 + juce::Random::getSystemRandom().nextInt (9999);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
        param->endChangeGesture();
    }
}

void Content::timerCallback()
{
    field.tick();
    meters.tick();
    outMeter.tick();
    room.tick();
    if (! seedValue.isBeingEdited())
        seedValue.setText (juce::String ((int) proc.apvts.getRawParameterValue ("seed")->load()), juce::dontSendNotification);
}

void Content::paint (juce::Graphics& g)
{
    // background with warm vignette + subtle noise texture
    g.fillAll (Colours::bg);
    juce::ColourGradient top (juce::Colour (0xff2a0e0c), 160.0f, 0.0f, juce::Colours::transparentBlack, 520.0f, 260.0f, true);
    g.setGradientFill (top);
    g.fillAll();
    juce::Random rr (99);
    for (int i = 0; i < 1400; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.02f * rr.nextFloat()));
        g.fillRect ((float) rr.nextInt (kDesignW), (float) rr.nextInt (kDesignH), 1.0f, 1.0f);
    }

    // seed box
    auto seedBox = juce::Rectangle<float> (464, 30, 130, 36);
    g.setColour (juce::Colour (0xff120e0e));
    g.fillRoundedRectangle (seedBox, 6.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (seedBox, 6.0f, 1.0f);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (13.0f));
    g.drawText ("Seed:", seedBox.withTrimmedLeft (12.0f), juce::Justification::centredLeft);

    g.setColour (Colours::textDim.withAlpha (0.6f));
    g.setFont (uiFont (11.0f, false, 0.2f));
    g.drawText ("v0.1  PROTOTYPE", juce::Rectangle<float> (900, 34, 200, 28), juce::Justification::centredRight);

    for (auto& pnl : panels)
    {
        auto b = pnl.bounds.toFloat();
        fillPanel (g, b);
        drawIcon (g, pnl.icon, juce::Rectangle<float> (b.getX() + 16, b.getY() + 15, 16, 16), Colours::red);
        g.setColour (Colours::red.brighter (0.2f));
        g.setFont (uiFont (14.0f, true, 0.06f));
        g.drawText (pnl.title, juce::Rectangle<float> (b.getX() + 42, b.getY() + 10, 200, 26), juce::Justification::centredLeft);
        g.setColour (Colours::panelEdge);
        g.drawHorizontalLine ((int) b.getY() + 42, b.getX() + 14, b.getRight() - 14);
    }
}

void Content::paintOverChildren (juce::Graphics& g)
{
    // knob captions
    for (auto& kp : knobs)
    {
        auto& k = *kp;
        auto sb = k.slider.getBounds().toFloat();
        const auto value = k.slider.getTextFromValue (k.slider.getValue());
        if (k.side)
        {
            auto t = juce::Rectangle<float> (sb.getRight() + 10.0f, sb.getY(), 100.0f, sb.getHeight());
            const float bigH = k.big ? 34.0f : 22.0f;
            auto block = t.withSizeKeepingCentre (t.getWidth(), 14.0f + bigH + 14.0f);
            g.setColour (Colours::text.withAlpha (0.85f));
            g.setFont (uiFont (11.5f, true, 0.06f));
            g.drawText (k.label, block.removeFromTop (14.0f), juce::Justification::left);
            g.setColour (Colours::text);
            g.setFont (uiFont (bigH, true));
            g.drawText (k.big ? value.upToFirstOccurrenceOf (" ", false, false) : value, block.removeFromTop (bigH), juce::Justification::left);
            g.setColour (Colours::textDim);
            g.setFont (uiFont (11.0f));
            g.drawText (k.range, block, juce::Justification::left);
        }
        else
        {
            g.setColour (Colours::text.withAlpha (0.85f));
            g.setFont (uiFont (10.5f, true, 0.05f));
            g.drawText (k.label, juce::Rectangle<float> (sb.getCentreX() - 50, sb.getY() - 16, 100, 14), juce::Justification::centred);
            g.setColour (Colours::text);
            g.setFont (uiFont (13.5f, true));
            g.drawText (value, juce::Rectangle<float> (sb.getCentreX() - 50, sb.getBottom() + 1, 100, 16), juce::Justification::centred);
        }
    }

    if (dragOver)
    {
        auto b = getLocalBounds().toFloat().reduced (6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (Colours::ember);
        g.drawRoundedRectangle (b, 12.0f, 2.0f);
        g.setFont (uiFont (28.0f, true, 0.1f));
        g.drawText ("DROP TAKES TO BUILD THE CROWD", b, juce::Justification::centred);
    }

    // balance caption
    auto bb = balance.getBounds().toFloat();
    g.setColour (Colours::text.withAlpha (0.85f));
    g.setFont (uiFont (11.5f, true, 0.06f));
    g.drawText ("M / F BALANCE", juce::Rectangle<float> (bb.getX(), bb.getY() - 40, 150, 14), juce::Justification::left);
    g.setColour (Colours::text);
    g.setFont (uiFont (20.0f, true));
    g.drawText (balance.getTextFromValue (balance.getValue()), juce::Rectangle<float> (bb.getX(), bb.getY() - 26, 150, 22), juce::Justification::left);
    g.setColour (Colours::male);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("M", juce::Rectangle<float> (bb.getX() - 14, bb.getY(), 12, bb.getHeight()), juce::Justification::centred);
    g.setColour (Colours::female);
    g.drawText ("F", juce::Rectangle<float> (bb.getRight() + 2, bb.getY(), 12, bb.getHeight()), juce::Justification::centred);
}

void Content::resized()
{
    logo.setBounds (8, 4, 320, 80);
    rerollButton.setBounds (336, 30, 118, 36);
    seedValue.setBounds (510, 30, 80, 36);

    field.setBounds (244, 92, 580, 374);
    meters.setBounds (834, 92, 274, 214);
    takesPanel.setBounds (834, 92, 274, 214);
    addButton.setBounds (608, 30, 140, 36);
    room.setBounds (774, 476, 334, 214);

    // CROWD
    knobs[0]->slider.setBounds (24, 140, 104, 104);  // voices
    knobs[1]->slider.setBounds (40, 252, 66, 66);    // takes
    knobs[2]->slider.setBounds (40, 324, 66, 66);    // male takes
    balance.setBounds (40, 436, 170, 20);

    // TIME & PITCH: delay min / delay spread / scatter on top, pitch spread / drift / loose below
    const int tp[2][3] = { { 3, 4, 16 }, { 5, 6, 17 } };
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < 3; ++i)
            knobs[(size_t) tp[row][i]]->slider.setBounds (58 + i * 118, row == 0 ? 538 : 622, 44, 44);

    // COLOR & SPACE: 3 on top, 4 below
    for (int i = 0; i < 3; ++i)
        knobs[(size_t) (7 + i)]->slider.setBounds (438 + i * 112, 538, 44, 44);
    for (int i = 0; i < 4; ++i)
        knobs[(size_t) (10 + i)]->slider.setBounds (420 + i * 88, 622, 44, 44);

    // OUTPUT
    knobs[14]->slider.setBounds (862, 372, 62, 62);
    knobs[15]->slider.setBounds (960, 372, 62, 62);
    outMeter.setBounds (1060, 358, 30, 96);
}

//==============================================================================
HordeEditor::HordeEditor (HordeProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (horde::ui::kDesignW * 6 / 10, horde::ui::kDesignH * 6 / 10,
                     horde::ui::kDesignW * 2, horde::ui::kDesignH * 2);
    getConstrainer()->setFixedAspectRatio ((double) horde::ui::kDesignW / horde::ui::kDesignH);
    setSize (horde::ui::kDesignW, horde::ui::kDesignH);
}

void HordeEditor::resized()
{
    const float s = (float) getWidth() / (float) horde::ui::kDesignW;
    content.setTransform (juce::AffineTransform::scale (s));
}
