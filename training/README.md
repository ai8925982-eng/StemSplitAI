# Model contract (สัญญาระหว่างโมเดลกับปลั๊กอิน)

ปลั๊กอินโหลดโมเดล `.onnx` ที่มีรูปแบบนี้เท่านั้น:

| | ชื่อ (ไม่บังคับ) | shape | dtype |
|---|---|---|---|
| input  | `mix`   | `[1, 2, T]` เสียงสเตอริโอรวม | float32 |
| output | `stems` | `[1, 30, 2, T]` 30 stem × สเตอริโอ | float32 |

* `T` ตายตัวหรือ dynamic ก็ได้ (ถ้า dynamic ปลั๊กอินใช้ T = 343 980)
* ใส่ ONNX metadata `sample_rate` (เช่น `44100`) ถ้าโมเดลไม่ได้เทรนที่ 44.1 kHz
* ลำดับ 30 stem (index 0 → 29) ต้องตรงกับ `Source/StemDefs.h`:

```
0 kick        1 snare       2 hihat       3 tom         4 cymbal
5 overhead    6 bass        7 synthbass   8 acoustic    9 electric
10 gtrsolo    11 powerchord 12 rhythm     13 piano      14 epiano
15 synth      16 organ      17 pad        18 phin       19 khaen
20 wot        21 ponglang   22 isanwind   23 violin     24 strings
25 brass      26 fx         27 perc       28 vocals     29 chorus
```

ตรวจโมเดลก่อนใช้: `python check_model.py my_model.onnx`

## เรื่องข้อมูลเทรน (สำคัญ)
ไม่มีโมเดลสาธารณะที่แยกได้ครบ 30 ชิ้นนี้ โดยเฉพาะ พิณ / แคน / โหวด / โปงลาง / เครื่องเป่าอีสาน
ต้องมีข้อมูลเทรนที่เป็น multitrack จริงหรือสังเคราะห์ (ตัวอย่างเสียงเดี่ยวของเครื่องดนตรีอีสานมาผสมเอง)
สถาปัตยกรรมที่เป็นจุดเริ่มต้น: HTDemucs / BS-RoFormer / MDX23 ปรับจำนวน target เป็น 30 แล้ว export เป็น ONNX
(`torch.onnx.export`, opset 17+, input `mix`, output `stems`)
