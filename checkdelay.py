from scapy.all import rdpcap, IP, Raw
import os

def analyze_latency_final(pcap_file, phone_ip, broker_ip, esp_ip):
    if not os.path.exists(pcap_file):
        print(f"Khong tim thay file: {pcap_file}")
        return

    packets = rdpcap(pcap_file)
    # Dung list de luu cac lenh theo thu tu thoi gian
    queue_app_to_broker = [] 
    results = []

    print(f"{'Event':<25} | {'Content':<20} | {'Timestamp'}")
    print("-" * 75)

    for pkt in packets:
        if pkt.haslayer(IP) and pkt.haslayer(Raw):
            src = pkt[IP].src
            dst = pkt[IP].dst
            
            # Giai ma payload va loc cac ky tu rac cua giao thuc MQTT
            try:
                raw_data = pkt[Raw].load.decode('utf-8', errors='ignore')
            except:
                continue

            # Chi quan tam den cac goi tin chua topic dieu khien (Loai bo PING/Keep-alive)
            if "esp32s3/" in raw_data and any(cmd in raw_data for cmd in ["ON", "OFF"]):
                
                # 1. App -> Broker
                if src == phone_ip and dst == broker_ip:
                    queue_app_to_broker.append({'time': pkt.time, 'data': raw_data})
                    print(f"App -> Broker (Publish) | {raw_data[-10:].strip():<20} | {pkt.time}")

                # 2. Broker -> ESP32
                elif src == broker_ip and dst == esp_ip:
                    if queue_app_to_broker:
                        # Khop goi tin theo nguyen tac: Goi tin den ESP som nhat tuong ung voi lenh gui di som nhat
                        match = queue_app_to_broker.pop(0)
                        latency = float(pkt.time - match['time']) * 1000
                        results.append(latency)
                        
                        print(f"Broker -> ESP32 (Forward) | {raw_data[-10:].strip():<20} | {pkt.time}")
                        print(f" >> [DO TRE]: {latency:.3f} ms")
                        print("-" * 75)

    if results:
        print(f"KET QUA CUOI CUNG:")
        print(f"- So lenh hop le: {len(results)}")
        print(f"- Do tre trung binh: {sum(results)/len(results):.3f} ms")
    else:
        print("Van chua khop duoc goi tin. Hay kiem tra lai IP ESP32 trong Wireshark.")

# Chay voi thong so cua ban
analyze_latency_final("capture.pcapng", "10.10.0.100", "172.168.1.2", "10.10.0.50")