"""TCP syslog/IOPShell receiver, protocol supplied by console owner.
Unicast beacons from the selected PC address; commands consumed sequentially.
"""
import argparse
import socket
import threading
import time
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--pc',default='192.168.31.14')
p.add_argument('--console',action='append',required=True)
p.add_argument('--directory',required=True)
a=p.parse_args()
root=Path(a.directory);root.mkdir(parents=True,exist_ok=True)
commands=root/'commands.txt';commands.touch(exist_ok=True)
log=open(root/'console.log','ab',buffering=0)
lock=threading.Lock();connected=threading.Event()
def write(data):
    if isinstance(data,str):data=data.encode('utf-8',errors='replace')
    stamp=time.strftime('%Y-%m-%d %H:%M:%S')
    with lock:log.write(stamp.encode()+b' '+data+b'\n')
def announce():
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.bind((a.pc,0))
        while True:
            if not connected.is_set():
                for ip in a.console:
                    try:s.sendto(b'WIIU_TCP_SYSLOG_BEACON_V1',(ip,4445))
                    except OSError as e:write(f'BEACON_ERROR {e}')
            time.sleep(.5)
def send(client,alive):
    beat=0
    while alive.is_set():
        try:
            if time.monotonic()-beat>=1:
                client.sendall(b'\0');beat=time.monotonic()
            if commands.stat().st_size:
                with commands.open('r+b') as f:
                    lines=f.read().splitlines();f.seek(0);f.truncate()
                for line in lines:
                    if line.strip():client.sendall(line+b'\r\n');write(b'COMMAND_SENT '+line)
        except OSError as e:
            write(f'SEND_ERROR {e}');alive.clear()
            try:client.shutdown(socket.SHUT_RDWR)
            except OSError:pass
        time.sleep(.1)
server=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
server.bind((a.pc,7965));server.listen(1)
write(f'LISTENING {a.pc}:7965 consoles={a.console}')
threading.Thread(target=announce,daemon=True).start()
while True:
    client,peer=server.accept()
    if peer[0] not in a.console:
        write(f'REJECTED {peer}');client.close();continue
    write(f'CONSOLE_CONNECTED {peer[0]}');connected.set()
    alive=threading.Event();alive.set()
    sender=threading.Thread(target=send,args=(client,alive),daemon=True);sender.start()
    pending=b''
    try:
        while alive.is_set():
            data=client.recv(65536)
            if not data:break
            pending+=data.replace(b'\0',b'').replace(b'\r\n',b'\n').replace(b'\r',b'\n')
            lines=pending.split(b'\n');pending=lines.pop()
            for line in lines:write(line)
    except OSError as e:write(f'CONSOLE_ERROR {e}')
    finally:
        alive.clear();client.close();sender.join(timeout=2);connected.clear()
        if pending:write(pending)
        write('CONSOLE_DISCONNECTED')
