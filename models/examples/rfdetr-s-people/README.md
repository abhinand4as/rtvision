<!-- SPDX-License-Identifier: Apache-2.0 -->
# rfdetr-s-people

People detection with **RF-DETR Small** (Roboflow, **Apache-2.0**): the network of
`../rfdetr-s-coco`, filtered to `person` (`classes.keep`) with a 0.4 threshold, publishing to the
Kafka topic `people-raw`.

It reads `../rfdetr-s-coco/rfdetr-small.onnx` and `../rfdetr-s-coco/labels.txt`, so export once:

```bash
tools/export/rfdetr/export.sh
PERCEPTRA_MODEL=rfdetr-s-people docker compose -f deploy/compose/compose.yaml up -d
```

The engine cache is per model name, so this model builds its own engine on first start.
