# StemSplit AI — VST3 + Standalone, แยกเสียง 30 stem

ปลั๊กอิน JUCE/CMake (C++17) ได้ 2 รูปแบบจากโค้ดชุดเดียว: **VST3** และ **Standalone**

## 30 stem
Kick · Snare · Hi-hat · Tom · Crash/Ride/Cymbal · Overhead/Room · Bass Guitar · Synth Bass ·
Acoustic Guitar · Electric Guitar · Guitar Solo · Power Chord · Rhythm Guitar · Piano · Electric Piano ·
Synthesizer · Organ · Pad · **พิณ · แคน · โหวด · โปงลาง · เครื่องเป่าอีสาน** · Violin · Strings · Brass ·
FX/Sound Effects · Percussion · Lead Vocals · Chorus/Backing Vocals (แยกเป็นคนละแท็ก)

## ทำอะไรได้
* **มิกเซอร์ 30 ช่อง** — เฟเดอร์ (−60…+12 dB), Mute, Solo, มิเตอร์ระดับเสียง (บันทึกอยู่ใน preset ของ DAW)
* **VST3 multi-out** — เอาต์พุต Stem Mix (สเตอริโอ) + 30 บัสสเตอริโอ (ปิดไว้เป็นค่าเริ่มต้น เปิดใน DAW เพื่อส่งแต่ละ stem ไปคนละแทร็ก; บัสพวกนี้ส่ง stem ดิบก่อนเฟเดอร์)
* **Standalone** — รับเสียงจากการ์ดเสียง, เปิดไฟล์เสียงมาฟัง (Open audio… → Play), และ **Export all stems (WAV)** เป็นไฟล์ 24-bit 30 ไฟล์ (render เร็วกว่าเรียลไทม์)
* **2 เอนจิน**
  1. **Built-in DSP** (ค่าเริ่มต้น, ไม่ต้องมีโมเดล) — STFT + แยก harmonic/percussive + วิเคราะห์ตำแหน่งสเตอริโอ + สเปกตรัมคาดการณ์ของแต่ละ stem; ผลรวมของ stem = เสียงต้นฉบับ latency ≈ 140 ms
  2. **ONNX AI model** (ต้อง build ด้วย `-DSTEMSPLIT_ONNX=ON` และมีไฟล์โมเดล) — ดู `training/README.md`

## ⚠️ ข้อจำกัดที่ต้องรู้
* **เอนจิน DSP เป็นแค่ heuristic** แยกตามย่านความถี่/ลักษณะเสียง/ตำแหน่งสเตอริโอ จึงแยกกลอง, เบส, เสียงร้อง ได้พอใช้ แต่ **แยกกีตาร์ไฟฟ้า vs ซินธ์, หรือ พิณ vs แคน vs โหวด ออกจากกันจริง ๆ ไม่ได้** (เสียงที่ย่านความถี่ใกล้กันจะถูกแบ่งกันตามน้ำหนักสมมติ) ปุ่ม **Isolation** ช่วยลดเสียงรั่วระหว่าง stem ได้ (ยิ่งสูงยิ่งรั่วน้อย แต่เสียงเริ่มมี artefact) — เสียงรั่วเป็น 0 จริง ๆ ทำไม่ได้ด้วย DSP ต้องใช้โมเดล AI ที่เทรนโดยลงโทษการรั่ว
* การแยกครบ 30 ชิ้นอย่างแม่นยำต้องใช้ **โมเดล AI ที่เทรนมาเฉพาะ** ซึ่งยังไม่มีเผยแพร่สาธารณะ (โดยเฉพาะเครื่องดนตรีอีสาน) — โปรเจกต์นี้เตรียมช่องเสียบไว้ให้แล้ว (contract ใน `training/README.md`) แต่ **ไม่มีไฟล์โมเดลแถมมา**
* โหมด ONNX มี latency หลายวินาที (≈ 1.5 × ความยาว chunk ของโมเดล) และกิน RAM หลายร้อย MB เหมาะกับ Export / bounce มากกว่าใช้สด
* การแพ็ก `onnxruntime.dll` ให้ VST3 บน Windows โหลดเจอใน DAW ทุกตัวอาจต้องปรับเพิ่ม (Standalone ไม่มีปัญหา)
* **โค้ดชุดนี้ผมเขียนแล้วแต่ยัง "ไม่ได้คอมไพล์/ทดสอบ" จริง** (สภาพแวดล้อมที่เขียนไม่มีเน็ตโหลด JUCE) คาดว่ารอบแรกบน GitHub Actions อาจมี error เล็ก ๆ ให้แก้ — ส่ง log มาได้เลย

## Build บนเครื่อง
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
ผลลัพธ์อยู่ที่ `build/StemSplit_artefacts/Release/VST3/` และ `.../Standalone/`
(Linux ต้องมี libasound2-dev, libx11-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libfreetype-dev, libfontconfig1-dev, libgl1-mesa-dev)

เปิด AI backend:
```bash
# โหลด onnxruntime release จาก github.com/microsoft/onnxruntime/releases แล้วแตกไฟล์
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSTEMSPLIT_ONNX=ON -DONNXRUNTIME_ROOT=/path/to/onnxruntime-xxx
```

## ขึ้น GitHub + build อัตโนมัติ
```bash
cd StemSplitAI
git init && git add . && git commit -m "StemSplit AI"
git branch -M main
git remote add origin https://github.com/<ชื่อคุณ>/StemSplitAI.git
git push -u origin main
```
แล้วเปิดแท็บ **Actions** — workflow `Build StemSplit AI` จะ build Windows / macOS / Linux ให้ และเก็บไฟล์ zip ไว้ในหน้า run (Artifacts)
ติดแท็บ `git tag v0.1.0 && git push --tags` เพื่อสร้าง GitHub Release อัตโนมัติ
ต้องการ ONNX: Actions → Build StemSplit AI → Run workflow → ติ๊ก `with_onnx` (ทดลอง)

## ติดตั้ง VST3
* Windows: คัดลอก `StemSplit AI.vst3` ไป `C:\Program Files\Common Files\VST3\`
* macOS: `~/Library/Audio/Plug-Ins/VST3/` (ไฟล์ไม่ได้เซ็น ให้รัน `xattr -cr "StemSplit AI.vst3"` ก่อน)
* Linux: `~/.vst3/`

## โครงสร้างโค้ด
| ไฟล์ | หน้าที่ |
|---|---|
| `Source/StemDefs.h` | รายการ 30 stem (ลำดับนี้คือสัญญากับโมเดล AI) |
| `Source/DspSeparator.*` | เอนจิน DSP ในตัว |
| `Source/OnnxSeparator.*` | เอนจิน ONNX Runtime (chunk + overlap-add บน thread แยก) |
| `Source/PluginProcessor.*` | บัส, พารามิเตอร์, มิกเซอร์, export WAV, เล่นไฟล์ |
| `Source/PluginEditor.*` | หน้าจอ |

ลิขสิทธิ์: JUCE 8 ใช้ AGPLv3 หรือไลเซนส์เชิงพาณิชย์ — ถ้าจะขายปลั๊กอินต้องซื้อไลเซนส์ JUCE
