#include "ReaperBridge.h"

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wshadow-field-in-constructor", "-Wnon-virtual-dtor", "-Wzero-as-null-pointer-constant", "-Wunused-parameter")
JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4100)

#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>

namespace reaper
{
    using namespace Steinberg;

    // From the REAPER SDK (reaper_vst3_interfaces.h, zlib-style licence, Cockos Inc.)
    class IReaperHostApplication : public FUnknown // available from IHostApplication in REAPER v5.02+
    {
    public:
        virtual void* PLUGIN_API getReaperApi (CStringA funcname) = 0;
        virtual void* PLUGIN_API getReaperParent (uint32 w) = 0; // parent track(=1), take(=2), project(=3), fxdsp(=4), trackchan(=5)
        virtual void* PLUGIN_API reaperExtended (uint32 call, void* parm1, void* parm2, void* parm3) = 0;
        static const FUID iid;
    };

    DECLARE_CLASS_IID (IReaperHostApplication, 0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C)
    DEF_CLASS_IID (IReaperHostApplication)
}

JUCE_END_IGNORE_WARNINGS_MSVC
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

void ReaperBridge::setIHostApplication (Steinberg::FUnknown* host)
{
    if (host == nullptr)
        return;

    void* obj = nullptr;
    if (host->queryInterface (reaper::IReaperHostApplication::iid, &obj) == Steinberg::kResultOk && obj != nullptr)
        reaper = obj;
}

void* ReaperBridge::getApi (const char* name) const
{
    return reaper != nullptr ? static_cast<reaper::IReaperHostApplication*> (reaper)->getReaperApi (name) : nullptr;
}

static bool looksFemale (juce::String name)
{
    name = name.toLowerCase();
    for (auto* w : { "fem", "woman", "girl", "жен", "дев", "жін", "дів" })
        if (name.contains (juce::String::fromUTF8 (w)))
            return true;

    juce::StringArray tokens;
    tokens.addTokens (name, " _-.()[]0123456789", "");
    for (auto& t : tokens)
        if (t == "f" || t == juce::String::fromUTF8 ("ж") || t == "w")
            return true;
    return false;
}

ReaperBridge::Result ReaperBridge::linkChildTracks()
{
    Result r;
    if (reaper == nullptr)
    {
        r.message = "Track linking works only inside REAPER (VST3 version of HORDE).";
        return r;
    }

    auto* host = static_cast<reaper::IReaperHostApplication*> (reaper);
    void* me   = host->getReaperParent (1);
    void* proj = host->getReaperParent (3);

    using GetValFn    = double (*) (void*, const char*);
    using SetValFn    = bool   (*) (void*, const char*, double);
    using CountFn     = int    (*) (void*);
    using GetTrackFn  = void*  (*) (void*, int);
    using ParentFn    = void*  (*) (void*);
    using NumSendsFn  = int    (*) (void*, int);
    using SendInfoFn  = void*  (*) (void*, int, int, const char*, void*);
    using RemoveFn    = bool   (*) (void*, int, int);
    using CreateFn    = int    (*) (void*, void*);
    using SetSendFn   = bool   (*) (void*, int, int, const char*, double);
    using NameFn      = bool   (*) (void*, const char*, char*, bool);
    using UndoBeginFn = void   (*) (void*);
    using UndoEndFn   = void   (*) (void*, const char*, int);
    using AdjustFn    = void   (*) (bool);

    auto getVal    = (GetValFn)    getApi ("GetMediaTrackInfo_Value");
    auto setVal    = (SetValFn)    getApi ("SetMediaTrackInfo_Value");
    auto count     = (CountFn)     getApi ("CountTracks");
    auto getTrack  = (GetTrackFn)  getApi ("GetTrack");
    auto parentOf  = (ParentFn)    getApi ("GetParentTrack");
    auto numSends  = (NumSendsFn)  getApi ("GetTrackNumSends");
    auto sendInfo  = (SendInfoFn)  getApi ("GetSetTrackSendInfo");
    auto removeSnd = (RemoveFn)    getApi ("RemoveTrackSend");
    auto createSnd = (CreateFn)    getApi ("CreateTrackSend");
    auto setSend   = (SetSendFn)   getApi ("SetTrackSendInfo_Value");
    auto nameOf    = (NameFn)      getApi ("GetSetMediaTrackInfo_String");
    auto undoBegin = (UndoBeginFn) getApi ("Undo_BeginBlock2");
    auto undoEnd   = (UndoEndFn)   getApi ("Undo_EndBlock2");
    auto adjust    = (AdjustFn)    getApi ("TrackList_AdjustWindows");

    if (me == nullptr || ! (getVal && setVal && count && getTrack && parentOf && numSends && sendInfo
                            && removeSnd && createSnd && setSend && nameOf))
    {
        r.message = "Could not reach REAPER's track functions (old REAPER version?).";
        return r;
    }

    // Collect direct children of the track HORDE sits on
    struct Child { void* track; bool female; juce::String name; };
    std::vector<Child> kids;
    const int n = count (proj);
    for (int i = 0; i < n; ++i)
    {
        void* t = getTrack (proj, i);
        if (t != nullptr && parentOf (t) == me)
        {
            char buf[512] = {};
            nameOf (t, "P_NAME", buf, false);
            const auto name = juce::String::fromUTF8 (buf);
            kids.push_back ({ t, looksFemale (name), name });
        }
    }

    if (kids.empty())
    {
        r.message = "No tracks inside this folder.\n\nMake the HORDE track a folder: put the take tracks right under it "
                    "and indent them (drag them slightly to the right, or set the folder icon). Then press LINK again.";
        return r;
    }

    // Male takes on the first inputs, female after them (that's how the plugin knows the gender)
    std::stable_sort (kids.begin(), kids.end(), [] (const Child& a, const Child& b) { return ! a.female && b.female; });

    if (undoBegin) undoBegin (proj);

    for (size_t i = 0; i < kids.size(); ++i)
    {
        void* t = kids[i].track;

        // remove our previous sends from this child
        for (int s = numSends (t, 0) - 1; s >= 0; --s)
            if (sendInfo (t, 0, s, "P_DESTTRACK", nullptr) == me)
                removeSnd (t, 0, s);

        // child no longer sums into the folder directly; it feeds its own HORDE input instead
        setVal (t, "B_MAINSEND", 0.0);

        const int idx = createSnd (t, me);
        if (idx >= 0)
        {
            const int dest = (int) (i % 8);
            setSend (t, 0, idx, "I_SRCCHAN", 0.0);                       // track channels 1-2
            setSend (t, 0, idx, "I_DSTCHAN", (double) (dest | 1024));    // mixed to mono, into input <dest+1>
            setSend (t, 0, idx, "I_SENDMODE", 0.0);                      // post-fader
            setSend (t, 0, idx, "D_VOL", 1.0);
        }

        if (kids[i].female) ++r.females; else ++r.males;
    }

    const int used = juce::jmin ((int) kids.size(), 8);
    const int need = juce::jmax (2, ((used + 1) / 2) * 2);
    if ((int) getVal (me, "I_NCHAN") < need)
        setVal (me, "I_NCHAN", (double) need);

    if (undoEnd) undoEnd (proj, "HORDE: link take tracks", -1);
    if (adjust) adjust (false);

    r.ok = true;
    r.message = "Linked " + juce::String ((int) kids.size()) + " take track(s): "
              + juce::String (r.males) + " male, " + juce::String (r.females) + " female.";
    if (kids.size() > 8)
        r.message << "\nHORDE has 8 inputs, so tracks 9+ share inputs with the first ones.";
    r.message << "\n\nGender is guessed from track names (female if the name has F, fem, woman, жен...). "
                 "Adjust with the MALE INPUTS knob if needed. Press LINK again after adding tracks.";
    return r;
}
