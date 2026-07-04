## What?

Eary's Spatial Expander is an free and open source **frequency-domain spatial upmixer** VST3 plugin. It runs a Short-Time Fourier Transform (STFT) on the input, performs a series of "phantom-center extraction" passes inside the frequency domain, and then converts each resulting channel back to audio with its own inverse transform. The result is up to 9 discrete spatial channels plus LFE, each containing a full-range, coherent signal.

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
Load Spatial Expander on a surround track or bus that contains the original stereo signals (for most VST hosts, they expect the input and output format to be the same. Cantabile Lite being node-based and accepting arbitrary input and output is an exception). 

**Input Format:** Select **Stereo**, **5.1**, or **7.1** to match your source. In Stereo mode, only the first two channels (L/R) are processed. In 5.1 or 7.1 mode, the plugin performs back-center extraction and builds a synthetic stereo field from all bed channels before running the cascade.

Recognized input formats are **stereo, 3.0, 5.1, 7.1, 5.1.x, 7.1.x and 9.1.x**. If your input has more than 2 channels while Stereo mode is selected, a warning will appear.

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
The **Quality/Latency** selector offers five modes:

| Mode | Latency (samples) | Latency @ 48 kHz |
|------|-------------------|------------------|
| Low | 496 | **10.3 ms** |
| Low-Mid | 992 | **20.7 ms** |
| Mid | 1984 | **41.3 ms** |
| High | 3968 | **82.7 ms** |
| Bypass/Realtime | 0 | **0 ms** |

Higher latency = larger FFT = better frequency resolution and cleaner extraction. Most offline DAWs automatically compensates for the reported latency, but realtime hosts like Cantabile Lite do not, so be ware.

**Bypass/Realtime** passes audio through unchanged with zero latency and minimal CPU. This is useful for A/B comparison.

**Note:** Switching latency, output format, 5.1 surrounds as rears mode, crosstalk, stretch, or rear bias during playback causes a brief recalibration pause (~50–100 ms) after you release the control.

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

### Input Format
Selects the source bed layout: **Stereo**, **5.1**, or **7.1**. This determines how the plugin interprets incoming channels. In 5.1 or 7.1 mode, the plugin extracts the back-center phantom image and remaps all bed channels into a synthetic stereo field before the cascade, preserving the original spatial relationships.

### Output Format
Selects the surround bed size. **Auto** is recommended—it detects the channel layout from the loaded bus and adapts the extraction cascade accordingly.

### 5.1 Surrounds as Rears in 7.1/9.1
When using a 5.1 output format on a 7.1/9.1 bus, this checkbox controls how the 5.1 surround channels are routed and calibrated. It also affects **input extraction** when reading 5.1 content from a 7.1/9.1 container.

**Output Behavior:**
- **Checked:** The 5.1 surround channels are treated as **rear** channels. They route to the rear outputs of the bus, and the calibration applies ITU-R BS.1770-4 rear channel weighting (0 dB).
- **Unchecked (default):** The 5.1 surround channels are treated as **side** channels. They route to the side outputs of the bus, and the calibration applies ITU-R BS.1770-4 side channel weighting (+1.5 dB).

**Input Behavior (5.1 Input Format on 7.1/9.1 container):**
- **Checked:** Prefer rear channel types (`leftSurroundRear`/`rightSurroundRear`) when extracting surround content.
- **Unchecked:** Prefer side channel types (`leftSurroundSide`/`rightSurroundSide`) when extracting surround content.

This is useful when you have a 7.1/9.1 bus but want to output 5.1 content: checking the box places the surrounds at the rear positions (standard 5.1 listening experience), while unchecking places them at the side positions (wider sound stage).

**Note:** This parameter only affects 5.1 output format on buses with separate rear channels (7.1/9.1). For pure 5.1 buses, the surrounds always use standard 5.1 side weighting (+1.5 dB). Changing this triggers a recalibration.

### Quality / Latency
The plugin does have latency, and it's required by the algorithm. It needs larger latency budget for higher sound quality. The UI shows the latency in both **samples** and **milliseconds** (updated dynamically for your session sample rate). There are four tiers of quality/latency modes plus a Bypass mode to choose from, default to Low-Mid tier.

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
Controls how the rear channels are derived. At **0.0**, the rear channels are fully isolated from the center signal, giving maximum clarity. At **1.0**, the raw unextracted residual is used, giving the rear channels a stronger, more ambient presence. The default **0.5** blends both for a smooth transition. Lower values give cleaner rear imaging; higher values give more envelopment. This is for creative control. The blend is a linear complex mix; loudness is kept consistent automatically by the calibration system, which includes rear bias when building the ILD gain table. Changing this triggers a recalibration.

### Crosstalk
Leaks each speaker's signal to its adjacent neighbors for smoother panning transitions. Range: **0.0 – 0.5**. Default: **0.1**. At 0, channels are completely discrete. Higher values create a more blended, continuous surround field. Applied in the frequency domain before calibration so the gain table automatically compensates for any loudness change. Changing this triggers a recalibration.

### Per-Channel Gain
Click **"Per-Channel Gain"** to reveal individual ±12 dB trims for every speaker channel:

Center, Front L, Front R, Wide L, Wide R, Side L, Side R, Rear L, Rear R

These are applied **after** automatic calibration and are intended for content-level balancing only. A label reminds you that room and speaker calibration should be done in your receiver or audio interface.

---

## Loudness Meter

The plugin includes an integrated **ITU-R BS.1770-4** loudness meter showing:

- **Momentary** (400 ms)
- **Short-term** (3 s) and Short-term Max
- **Integrated** LUFS

The meter starts **paused** by default. Click **Resume** to begin accumulation, **Pause** to hold, and **Refresh** to reset the momentary, short-term, and integrated histories. This is useful for verifying output levels after calibration.

---

## How the Algorithm Works

### 1. STFT Front End
The input signal is transformed into the frequency domain using a **Kaiser-Bessel window** (β = 0.5) with **96.875 % overlap** (hop size = FFT size / 32). The synthesis window is computed to satisfy the COLA (Constant Overlap-Add) constraint, ensuring perfect reconstruction.

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

### 3. The Cascade (Frequency-Domain-Only)
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

### 4. Multi-Channel Input Path (5.1 / 7.1)
When the Input Format is set to 5.1 or 7.1, the plugin first extracts the back-center phantom image from the surround/rear pair using the same minimum-magnitude rule. The residual surround channels and the front/center channels are then remapped into a **synthetic stereo** field via constant-power panning:

**Constant-power panning formula:**

```
r = 10^(Δ / 20)
L_gain = r / sqrt(1 + r²)
R_gain = 1 / sqrt(1 + r²)
```

Each input channel is assigned a fixed ILD Δ so that it lands in the correct spatial sector when fed into the existing stereo cascade:

**5.1 Input → Synthetic Stereo**

| 5.1 Channel | ILD Δ | L Gain | R Gain | Notes |
|-------------|-------|--------|--------|-------|
| **C** | **0 dB** | 0.707 | 0.707 | Center phantom |
| **FL** | **+6 dB** | 0.894 | 0.447 | Cascade extracts pure Front L at Layer 2 |
| **FR** | **−6 dB** | 0.447 | 0.894 | Cascade extracts pure Front R at Layer 2 |
| **SL′** | **+20 dB** | 0.995 | 0.100 | Residual after BC extraction; cascade dumps to Rear L |
| **SR′** | **−20 dB** | 0.100 | 0.995 | Residual after BC extraction; cascade dumps to Rear R |

**7.1 Input → Synthetic Stereo**

| 7.1 Channel | ILD Δ | L Gain | R Gain | Notes |
|-------------|-------|--------|--------|-------|
| **C** | **0 dB** | 0.707 | 0.707 | Center phantom |
| **FL** | **+6 dB** | 0.894 | 0.447 | Cascade extracts pure Front L at Layer 2 |
| **FR** | **−6 dB** | 0.447 | 0.894 | Cascade extracts pure Front R at Layer 2 |
| **Side L** | **+9.5 dB** | 0.949 | 0.317 | Cascade extracts pure Side L at Layer 3 |
| **Side R** | **−9.5 dB** | 0.317 | 0.949 | Cascade extracts pure Side R at Layer 3 |
| **RL′** | **+20 dB** | 0.995 | 0.100 | Residual after BC extraction; cascade dumps to Rear L |
| **RR′** | **−20 dB** | 0.100 | 0.995 | Residual after BC extraction; cascade dumps to Rear R |

Because constant-power panning ensures `L² + R² = 1` per channel, the summed synthetic stereo may have a different total energy than the original multi-channel bed. The plugin computes `sqrt(origEnergy / synEnergy)` and rescales both synthetic channels uniformly before the cascade, preserving the ILDs while preventing a loudness jump. After the cascade and calibration, the extracted back-center is reinjected into the rear speakers so rear-center content does not collapse to the front.

### 5. Phase Coherence Restoration
After the cascade, each derived channel's **phase** is restored from its original parent channel:

- **Front L, Side L, Wide L, Rear L** inherit the original **Left** input phase.
- **Front R, Side R, Wide R, Rear R** inherit the original **Right** input phase.
- **Center** is left as-is (it already has coherent L+R phase).

This preserves the natural stereo coherence of the original recording and prevents "phasiness" when the surround channels play together.

### 6. Spatial Stretching
The **Stretch** parameter remaps extracted channels to output speakers. At full stretch, each extracted layer feeds its own speaker. At zero stretch, surround layers are folded forward:

- **5.1:** Rear → Front
- **7.1:** Side → Center/Front, Rear → Front
- **9.1:** Wide → Center/Front, Side → Center/Front, Rear → Front

This is applied in the frequency domain before calibration.

### 7. Automatic Calibration
Because the cascade naturally changes total loudness as a source pans (a center-panned source splits into multiple correlated channels), the plugin runs an internal **automatic calibration** at startup and whenever you change format, latency, stretch, crosstalk, or rear bias.

*Calibration does not normalize content loudness, its job is to make the cascade preserve original loudness.*

**Calibration with perceptual weighting:**
The plugin generates correlated white noise with a flat spectrum, sweeps the pan from hard-left to hard-right in 0.1 dB ILD steps, and runs the full cascade (including stretch, rear bias, and crosstalk) at each step. It measures the total output power using **ITU-R BS.1770-4 / BS.2051 perceptual channel weightings** (+1.5 dB for side/wide/surround channels; front, center, and rear channels are weighted at 0 dB) so that phantom images between speakers sound equally loud, not just physically equal-energy. A gain table `G[ILD]` is built that normalizes every pan position to the same perceived loudness.

During playback, the plugin computes the ILD independently for every FFT bin, looks up the corresponding gain from the calibration table, and applies that gain only to that bin. A soft confidence gate based on the bin's energy relative to the frame peak gracefully fades the correction to unity for quiet bins, preventing grain on reverb tails. There is no temporal smoothing across frames—the STFT's 32× overlap-add provides natural temporal continuity. The result is a spectral shaper rather than a dynamics processor: no pumping, no broadband ducking.

There is no automatic peak limiter after calibration. If you are facing any clipping issue, use the preamp slider to adjust the input gain.

### 8. LFE Extraction
The LFE channel is derived from `(L+R)/2` passed through a **4th-order Bessel-Thomson low-pass filter** (two cascaded biquads). Bessel-Thomson was chosen because it has maximally flat group delay, making fixed-delay compensation accurate across the passband.

The LFE path is delayed by a compensation line so that its total latency matches the plugin's reported latency, keeping it sample-aligned with the main channels.

### 9. Leak Center, Crosstalk & Output
**Crosstalk** is applied inside the frequency-domain pipeline before calibration, so the gain table automatically compensates for any loudness change. It leaks each speaker's signal to its adjacent neighbors (e.g., Front L → Wide L → Side L → Rear L) using a loudness-preserving formula, smoothing spatial transitions. After the iSTFT, **Leak Center** redistributes the calibrated Center signal to the **Front L/R** speakers. Finally, channels are mapped to your DAW's output bus using standard channel type identifiers (Left, Right, Centre, LFE, Side, Rear, Wide, etc.) with intelligent index fallbacks for discrete/custom layouts.

---

##  How to Build
Again, the repo owner has only tried this on a Windows ×64 machine, so currently there would only be instruction for Windows. This is a JUCE project, so you would need JUCE. You would also need Visual Studio 2022. Open the *.jucer file with Projucer, follow the prompt to add JUCE dependencies, save JUCE project by clicking on Projucer's "Save Project and Open in IDE" option, Visual Studio 2022 should automatically launch, switch build mode to Release, right click on the `SpatialExpander_VST3` solution and select build or rebuild. The result VST3 would be in `\Builds\VisualStudio2022\x64\Release\VST3\SpatialExpander.vst3\Contents` directory. 
