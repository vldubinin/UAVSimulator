import cv2
import zmq
import numpy as np

context = zmq.Context()
socket = context.socket(zmq.SUB)
socket.connect("tcp://127.0.0.1:5555")
socket.setsockopt_string(zmq.SUBSCRIBE, '')

print("Waiting for JPEG frames from Unreal Engine...")

while True:
    frame_data = socket.recv()
    
    if len(frame_data) == 0:
        continue
        
    # Просто декодуємо JPEG байти напряму
    np_arr = np.frombuffer(frame_data, np.uint8)
    img = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)

    if img is not None:
        cv2.imshow("UAV Camera Stream", img)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

cv2.destroyAllWindows()