## What?

Eary's Spatial Expander is an free and open source **frequency-domain spatial upmixer** VST3 plugin. It runs a Short-Time Fourier Transform (STFT) on the stereo input, performs a series of "phantom-center extraction" passes inside the frequency domain, and then converts each resulting channel back to audio with its own inverse transform. The result is up to 9 discrete spatial channels plus LFE, each containing a full-range, coherent signal.

It supports **3.0, 5.1, 7.1, and 9.1** output formats (9.1 requires a 9.1.6 bus container even though the height channels are not used), and can automatically detect your DAW's surround bus layout.

Currently only tested on Windows ×64, since that's what the repo owner is using.

**Attribution:** This plugin was inspired by the repo owner's experience cascading instances of [Bertom Audio Phantom Center](https://bertomaudio.com/phantom-center.html) in [Cantabile Lite](https://www.cantabilesoftware.com/free-vst-host). The single-layer phantom-center extraction algorithm is a well-established technique; the multi-layer cascade and spatial stretching concept are original to this project.

---

## Why?

Most upmixers don't offer clear directional positioning of elements in the resulted surround sound field, directionality of the sounds are blurry, ambiguous, center vocals come from all the speakers at once instead of just the center channel. For the ones that do achieve this, they often do it without respecting the original spatial relationships between elements. 

Eary's Spatial Expander instead treats the stereo field as a set of relative positions and remaps those positions onto a surround ring. A source that was hard-left in stereo becomes hard-left in surround, which, on a 360° speaker ring, means rear-left. A source that was slightly left of center becomes side-left. The relative spatial relationships are preserved; only the speaker topology is stretched.

This technique treats Stereo panning not as a map of absolute angles, but as **relative extremity within the playback system**. Hard-left in a stereo pair does not mean "−30°"; it means **"the maximum leftward extent available."** In a 360° surround ring, that same maximum extent is rear-left (−150°). The cascade preserves the **relative spatial relationships** of the stereo sound field while adapting them to the speaker topology. The idea is that, if hard-panned left is only correctly respecting the original work by being at -30 degrees front, then playing it back with headphones or in-ear-monitors would be disrespecting it already, since the hard-panned sound would be directly in your ear. 

What the technique is basically doing (7.1 example):

<img width="1436" height="811" alt="image" src="https://github.com/user-attachments/assets/4200d491-4030-40d7-b69d-15978f1811ea" />

From the repo owner's limited experience, the result is really good and intuitive. 

Elements that the mixing engineer put to the widest positions are now in the rear, making those elements sounding as wide as possible. Those elements panned in between are put to the sides, and center vocals only stay at the center channel. For movie viewing, a character speaking from the left side off-screen, and then walk gradually from off-screen to on-screen, the plugin puts them first in the left rear channel, and then they gradually move to the left side surround speaker, and then front speakers; for outdoor scenes, ambient sounds also naturally get put in the rear; if a car runs from off-screen left to on-screen and then off-screen right, we hear the car sound sweeping from rear left to side left to front to side right and finally rear right. 

---

## How to use

### 1. Insert the plugin
Load Spatial Expander on a surround track or bus that contains the original stereo signals (for most VST hosts, they expect the input and output format to be the same. Cantabile Lite being node-based and accepting arbitrary input and output is an exception). The plugin accepts input with ≥ 2 channels, but **only the first two channels (L/R) are processed.** If your input has more than 2 channels, a warning will appear. Recognized input formats are **stereo, 3.0, 5.1, 7.1, 5.1.x, 7.1.x and 9.1.x**.

### 2. Set the Output Format
Open the plugin UI and choose your target layout:

| Format | Spatial Channels | Description |
|--------|------------------|-------------|
| **Auto** | Detected from bus | Reads the bus layout automatically. |
| **3.0** | Center, Front L, Front R | Minimal upmix. |
| **5.1** | + Rear L, Rear R | Classic surround. |
| **7.1** | + Side L, Side R | Adds side speakers between front and rear. |
| **9.1** | + Wide L, Wide R | Adds wide speakers between front and side. (requires 9.1.6 bus container)|

The combo box shows **Auto (DetectedFormat)**, that should satisfy most cases of track-bus style hosts. For Cantabile Lite users, the auto would just fallback to 5.1 since Cantabile Lite does not even have a bus concept, so manual selection of output format is needed. 

### 3. Choose your Quality / Latency trade-off
The **Quality/Latency** selector offers four STFT window sizes:

| Mode | Latency (samples) | Latency @ 48 kHz |
|------|-------------------|------------------|
| Low | 496 | **10.3 ms** |
| Low-Mid | 992 | **20.7 ms** |
| Mid | 1984 | **41.3 ms** |
| High | 3968 | **82.7 ms** |

Higher latency = larger FFT = better frequency resolution and cleaner extraction. Most offline DAWs automatically compensates for the reported latency, but realtime hosts like Cantabile Lite do not, so be ware.

**Note:** Switching latency, output format, crosstalk, or stretch during playback causes a brief recalibration pause (~50–100 ms) after you release the control. Rear Bias can be adjusted smoothly without any pause.

### 4. Adjust to taste
Use the parameters described below to shape the upmix. 

### 5. Routing in node-based hosts
If your host supports multi-bus plugins (e.g., Cantabile Lite), Spatial Expander exposes auxiliary output buses:
- **Front** (stereo)
- **Center** (mono)
- **LFE** (mono)
- **Wide** (stereo)
- **Side** (stereo)
- **Rear** (stereo)

You can route these to individual speaker channels or external processors.

---

## All Parameters Explained

### Output Format
Selects the surround bed size. **Auto** is recommended—it detects the channel layout from the loaded bus and adapts the extraction cascade accordingly.

### Quality / Latency
The plugin does have latency, and it's required by the algorithm. It needs larger latency budget for higher sound quality. The UI shows the latency in both **samples** and **milliseconds** (updated dynamically for your session sample rate). There are four tiers of quality/latency modes to choose from, default to Low-Mid tier.

### Stretch
Controls how aggressively the stereo field is stretched across the surround ring.

- **1.0 (Full Stretch):** Hard-panned sources go to the rear speakers. Moderate sources go to side/wide. This is the default and the intended "surround" experience.
- **0.0 (Collapsed):** All extracted surround content is folded back into the front LCR speakers. The surround speakers go silent.
- **In between:** Linear cross-fade. For example, at 0.5 in 7.1 mode, the side speakers are reduced by half and their energy is split between Center and Front L/R; rear speakers are split 50/50 with Front L/R.

**Note:** In **3.0 mode**, Stretch has no audible effect (there are no surround speakers to remap) and the control is disabled. Changing this triggers a recalibration.

### Leak Center
Redistributes the **Center speaker** energy to the **Front L/R** speakers. This is useful if you prefer a phantom center rather than a discrete isolated center speaker. Recommend using this when listening to pop songs. 

- **0.0:** True isolated center. 100 % of the center signal stays in the Center speaker.
- **1.0:** Equal LCR. Center, Front L, and Front R each receive an equal-energy share.
- **1.5:** Center reduced. ~75 % of the center power is moved to Front L/R; the Center speaker retains ~25 %.

**Important:** Leak Center is applied **after** calibration and **only** affects the front three speakers. Surround channels are unaffected.

### LFE Cutoff
Cutoff frequency for the LFE low-pass filter. Range: **40 Hz – 200 Hz**. Default: **80 Hz**.

### LFE Level
Gain of the LFE channel. Range: **−12.0 dB to +12.0 dB**. Default: **0.0 dB**. At far-left end, the slider displays **"−inf dB"** and the LFE channel is fully muted.

### Preamp
Input gain. Range: **−6.0 dB to +6.0 dB**. Applied before the STFT. Useful for gain-staging quiet or hot sources into the extractor.

### Rear Bias
Controls how the rear channels are derived. At **0.0**, the rear channels are fully isolated from the center signal, giving maximum clarity. At **1.0**, the raw unextracted residual is used, giving the rear channels a stronger, more ambient presence. The default **0.5** blends both for a smooth transition. Lower values give cleaner rear imaging; higher values give more envelopment. This is for creative control. The blend is magnitude-preserving, so loudness stays consistent across the entire range and no recalibration is needed.

### Crosstalk
Leaks each speaker's signal to its adjacent neighbors for smoother panning transitions. Range: **0.0 – 0.5**. Default: **0.2**. At 0, channels are completely discrete. Higher values create a more blended, continuous surround field. Applied in the frequency domain before calibration so the gain table automatically compensates for any loudness change. Changing this triggers a recalibration.

### Per-Channel Gain
Click **"Per-Channel Gain"** to reveal individual ±12 dB trims for every speaker channel:

Center, Front L, Front R, Wide L, Wide R, Side L, Side R, Rear L, Rear R

These are applied **after** automatic calibration and are intended for content-level balancing only. A label reminds you that room and speaker calibration should be done in your receiver or audio interface.

---

## How the Algorithm Works

### 1. STFT Front End
The input stereo signal is transformed into the frequency domain using a **Kaiser-Bessel window** (β = 0.5) with **96.875 % overlap** (hop size = FFT size / 32). The synthesis window is computed to satisfy the COLA (Constant Overlap-Add) constraint, ensuring perfect reconstruction.

This high overlap rate improves the processing resolution and minimizes the "watery" artifacts common in lower-overlap STFT processors. But note some amount of the artifacts will still be present for the lower quality/latency modes. 

### 2. Phantom Center Extraction
For every frequency bin, the algorithm compares the complex spectra of two input channels (e.g., Left and Right). It extracts the correlated "phantom center" using a **minimum-magnitude** rule:

```
aMag = |A|          bMag = |B|
cMag = min(aMag, bMag)

If cMag is significant:
    C = cMag * (A + B) / |A + B|
Else:
    C = 0

Residual A = A - C
Residual B = B - C
```

### 3. The Cascade (Frequency-Domain Only)
The extraction is repeated in successive layers, always operating on complex spectra—**never converting back to time domain between layers.**

| Layer | Inputs | Extracts |
|-------|--------|----------|
| 1 | (L, R) | Center, Lres, Rres |
| 2 | (Lres, Center) | Front L, Rear L, tempCenter |
| 2 | (Rres, tempCenter) | Front R, Rear R, **Final Center** |
| 3 | (Front L, Rear L) | Side L, Front L, Rear L |
| 3 | (Front R, Rear R) | Side R, Front R, Rear R |
| 4 | (Front L, Side L) | Wide L, Front L, Side L |
| 4 | (Front R, Side R) | Wide R, Front R, Side R |

After all layers, each final spectrum is converted to the time domain via its own inverse FFT. The result is up to 9 discrete channels plus LFE.

### 4. Phase Coherence Restoration
After the cascade, each derived channel's **phase** is restored from its original parent channel:

- **Front L, Side L, Wide L, Rear L** inherit the original **Left** input phase.
- **Front R, Side R, Wide R, Rear R** inherit the original **Right** input phase.
- **Center** is left as-is (it already has coherent L+R phase).

This preserves the natural stereo coherence of the original recording and prevents "phasiness" when the surround channels play together.

### 5. Spatial Stretching
The **Stretch** parameter remaps extracted channels to output speakers. At full stretch, each extracted layer feeds its own speaker. At zero stretch, surround layers are folded forward:

- **5.1:** Rear → Front
- **7.1:** Side → Center/Front, Rear → Front
- **9.1:** Wide → Center/Front, Side → Center/Front, Rear → Front

This is applied in the frequency domain before calibration.

### 6. Automatic Calibration
Because the cascade naturally changes total loudness as a source pans (a center-panned source splits into multiple correlated channels), the plugin runs an internal **automatic calibration** at startup and whenever you change format, latency, stretch, or crosstalk.

**Calibration with perceptual weighting:**
The plugin generates correlated white noise with a flat spectrum, sweeps the pan from hard-left to hard-right in 0.1 dB ILD steps, and runs the full cascade (including stretch, rear bias, and crosstalk) at each step. It measures the total output power using **ITU-R BS.1770-4 / BS.2051 perceptual channel weightings** (+1.5 dB for side/wide/surround channels; front, center, and rear channels are weighted at 0 dB) so that phantom images between speakers sound equally loud, not just physically equal-energy. A gain table `G[ILD]` is built that normalizes every pan position to the same perceived loudness.

During playback, the plugin computes the ILD independently for every FFT bin, looks up the corresponding gain from the calibration table, and applies that gain only to that bin. A soft confidence gate based on the bin's energy relative to the frame peak gracefully fades the correction to unity for quiet bins, preventing grain on reverb tails. There is no temporal smoothing across frames—the STFT's 32× overlap-add provides natural temporal continuity. The result is a spectral shaper rather than a dynamics processor: no pumping, no broadband ducking.

There is no automatic peak limiter after calibration. If you are facing any clipping issue, use the preamp slider to adjust the input gain.

### 7. LFE Extraction
The LFE channel is derived from `(L+R)/2` passed through a **4th-order Bessel-Thomson low-pass filter** (two cascaded biquads). Bessel-Thomson was chosen because it has maximally flat group delay, making fixed-delay compensation accurate across the passband.

The LFE path is delayed by a compensation line so that its total latency matches the plugin's reported latency, keeping it sample-aligned with the main channels.

### 8. Leak Center, Crosstalk & Output
**Crosstalk** is applied inside the frequency-domain pipeline before calibration, so the gain table automatically compensates for any loudness change. It leaks each speaker's signal to its adjacent neighbors (e.g., Front L → Wide L → Side L → Rear L) using a loudness-preserving formula, smoothing spatial transitions. After the iSTFT, **Leak Center** redistributes the calibrated Center signal to the **Front L/R** speakers. Finally, channels are mapped to your DAW's output bus using standard channel type identifiers (Left, Right, Centre, LFE, Side, Rear, Wide, etc.) with intelligent index fallbacks for discrete/custom layouts.

---

##  How to Build
Again, the repo owner has only tried this on a Windows ×64 machine, so currently there would only be instruction for Windows. This is a JUCE project, so you would need JUCE. You would also need Visual Studio 2022. Open the *.jucer file with Projucer, follow the prompt to add JUCE dependencies, save JUCE project by clicking on Projucer's "Save Project and Open in IDE" option, Visual Studio 2022 should automatically launch, switch build mode to Release, right click on the `SpatialExpander_VST3` solution and select build or rebuild. The result VST3 would be in `\Builds\VisualStudio2022\x64\Release\VST3\SpatialExpander.vst3\Contents` directory. 
