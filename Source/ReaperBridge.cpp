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

    using CreateAccFn  = void*  (*) (void*);
    using AccTimeFn    = double (*) (void*);
    using AccSamplesFn = int    (*) (void*, int, int, double, int, double*);
    using DestroyAccFn = void   (*) (void*);
    auto createAcc  = (CreateAccFn)  getApi ("CreateTrackAudioAccessor");
    auto accStart   = (AccTimeFn)    getApi ("GetAudioAccessorStartTime");
    auto accEnd     = (AccTimeFn)    getApi ("GetAudioAccessorEndTime");
    auto accSamples = (AccSamplesFn) getApi ("GetAudioAccessorSamples");
    auto destroyAcc = (DestroyAccFn) getApi ("DestroyAudioAccessor");
    if (! (createAcc && accStart && accEnd && accSamples && destroyAcc))
    {
        r.message = "Could not reach REAPER's audio reading functions (old REAPER version?).";
        return r;
    }

    if (undoBegin) undoBegin (proj);

    constexpr double readRate = 48000.0;
    constexpr int chunk = 48000;
    std::vector<double> buf ((size_t) chunk * 2);

    for (auto& kid : kids)
    {
        void* t = kid.track;

        // undo the old v0.3 routing (sends into HORDE) if it is still there
        for (int s = numSends (t, 0) - 1; s >= 0; --s)
            if (sendInfo (t, 0, s, "P_DESTTRACK", nullptr) == me)
                removeSnd (t, 0, s);

        // the takes are played by HORDE now, so they no longer sound directly through the folder
        setVal (t, "B_MAINSEND", 0.0);

        // read the whole track (all its items) as audio
        void* acc = createAcc (t);
        if (acc == nullptr) continue;
        const double t0 = accStart (acc), t1 = juce::jmin (accEnd (acc), accStart (acc) + 15.0 * 60.0);
        const int total = (int) ((t1 - t0) * readRate);
        if (total > (int) readRate / 2)
        {
            Result::TrackAudio ta;
            ta.name = kid.name.isNotEmpty() ? kid.name : juce::String ("Track");
            ta.female = kid.female;
            ta.sampleRate = readRate;
            ta.mono.setSize (1, total);
            float* dst = ta.mono.getWritePointer (0);
            for (int pos = 0; pos < total; pos += chunk)
            {
                const int n = juce::jmin (chunk, total - pos);
                std::fill (buf.begin(), buf.end(), 0.0);
                accSamples (acc, (int) readRate, 2, t0 + pos / readRate, n, buf.data());
                for (int i = 0; i < n; ++i)
                    dst[pos + i] = (float) (0.5 * (buf[(size_t) i * 2] + buf[(size_t) i * 2 + 1]));
            }
            if (ta.mono.getMagnitude (0, 0, total) > 1.0e-4f)
            {
                if (kid.female) ++r.females; else ++r.males;
                r.audio.push_back (std::move (ta));
            }
        }
        destroyAcc (acc);
    }

    if (undoEnd) undoEnd (proj, "HORDE: load take tracks", -1);
    if (adjust) adjust (false);

    if (r.audio.empty())
    {
        r.message = "The tracks inside the folder have no audio yet.";
        return r;
    }

    r.ok = true;
    r.message = "Loaded " + juce::String ((int) r.audio.size()) + " take track(s): "
              + juce::String (r.males) + " male, " + juce::String (r.females) + " female.\n\n"
              "Gender is guessed from track names (F, fem, woman...); click M/F in the list to change it. "
              "Press LINK again after recording or editing takes.";
    return r;
}
