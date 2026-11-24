import time
import requests
import cv2
import numpy as np
import tensorflow as tf

ESP32_BASE =  "http://10.157.181.60"
CAPTURE_URL = ESP32_BASE + "/capture"
PREDICTION_URL = ESP32_BASE + "/prediction"
MODEL_PATH = "Models/silkworm_disease_model_v3.tflite"

# image_dataset_from_directory sorts folder names alphabetically.
CLASS_NAMES = ["Grasserie", "Healthy", "Pebrine"]

print("Loading Eco-Silk Edge CNN model...")
interpreter = tf.lite.Interpreter(model_path=MODEL_PATH)
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()[0]
output_details = interpreter.get_output_details()[0]

print("Model loaded.")
print("Input shape:", input_details["shape"])
print("Input dtype:", input_details["dtype"])
print("Connecting to ESP32:", ESP32_BASE)

# Confirm the ESP32 is reachable before starting.
api_response = requests.get(ESP32_BASE + "/api", timeout=5)
api_response.raise_for_status()
print("ESP32 dashboard connection: OK")
print("Continuous CNN-to-dashboard detection started. Press Ctrl+C to stop.")

while True:
    try:
        # 1. Capture a fresh image from the ESP32.
        response = requests.get(CAPTURE_URL, timeout=10)
        response.raise_for_status()

        image_array = np.frombuffer(response.content, dtype=np.uint8)
        frame = cv2.imdecode(image_array, cv2.IMREAD_COLOR)

        if frame is None:
            print("Could not decode camera image; retrying.")
            time.sleep(1)
            continue

        # 2. Prepare image. The MobileNetV2 preprocess_input layer
        # is already INSIDE this converted model, so do not apply it again.
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        resized = cv2.resize(rgb, (224, 224))
        model_input = np.expand_dims(resized, axis=0)

        # Convert to the model's input dtype/quantization.
        if input_details["dtype"] == np.float32:
            model_input = model_input.astype(np.float32)
        else:
            scale, zero_point = input_details["quantization"]
            if scale <= 0:
                raise RuntimeError(
                    "Model input is quantized but has no valid scale."
                )
            model_input = np.round(model_input / scale + zero_point)
            limits = np.iinfo(input_details["dtype"])
            model_input = np.clip(
                model_input, limits.min, limits.max
            ).astype(input_details["dtype"])

        # 3. Run CNN.
        interpreter.set_tensor(input_details["index"], model_input)
        interpreter.invoke()
        scores = interpreter.get_tensor(output_details["index"])[0]

        # Dequantize output if needed.
        if output_details["dtype"] != np.float32:
            scale, zero_point = output_details["quantization"]
            scores = (
                scores.astype(np.float32) - zero_point
            ) * scale

        if len(scores) != len(CLASS_NAMES):
            raise RuntimeError(
                f"Expected 3 model outputs, received {len(scores)}."
            )

        index = int(np.argmax(scores))
        disease = CLASS_NAMES[index]
        confidence = float(scores[index]) * 100.0

        # 4. Send prediction to the ESP32 dashboard.
        payload = {
            "disease": disease,
            "confidence": round(confidence, 2)
        }
        post_response = requests.post(
            PREDICTION_URL, json=payload, timeout=5
        )
        post_response.raise_for_status()

        print(
            f"Dashboard updated: {disease} "
            f"({confidence:.1f}%)"
        )

        # Optional local preview. Press Q in the preview to stop.
        preview = frame.copy()
        cv2.putText(
            preview,
            f"{disease}: {confidence:.1f}%",
            (10, 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.75,
            (0, 255, 0),
            2
        )
        cv2.imshow("Eco-Silk Edge - CNN to Dashboard", preview)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

        # About 2 seconds between detections.
        time.sleep(2)

    except requests.RequestException as error:
        print("ESP32 communication error:", error)
        time.sleep(2)
    except Exception as error:
        print("Prediction error:", error)
        time.sleep(2)