from threading import Thread
import socket
import aiohttp
from aiohttp import web
import requests
import time
import json
import struct

visualiser_connection = None
# todo need mutex
# todo read/write frames from/to disk
frames = []
need_reset = True


async def send_message_to_visualiser_handler(request):
    global visualiser_connection
    global frames
    global need_reset

    ws = web.WebSocketResponse(compress=False, max_msg_size=0)
    await ws.prepare(request)
    visualiser_connection = ws

    print('Connected send_message_to_visualiser_handler')

    if need_reset:
        print("Send reset")
        await visualiser_connection.send_json(json.loads(json.dumps({"type": "reset"})))
        need_reset = False
    else:
        await visualiser_connection.send_json(
            json.loads(json.dumps({"type": "frames_number", "frames_number": len(frames)})))

    async for msg in ws:
        if msg.type == aiohttp.WSMsgType.TEXT:

            if msg.data == 'close':
                await ws.close()

            elif msg.data.startswith('get_frame'):
                frame_id = int(msg.data.split(' ')[1])

                t = time.time()
                c = time.clock()
                await ws.send_bytes(frames[frame_id])

                print("websocket send_str:", len(frames[frame_id]), "time:",
                      time.time() - t, "cpu:", time.clock() - c)

        elif msg.type == aiohttp.WSMsgType.ERROR:
            print('exception %s' % ws.exception())

    print('closed')

    visualiser_connection = None
    return ws


async def visualiser_page(request):
    return web.FileResponse("./debug_viewer123.html")


async def resend_data(request):
    global visualiser_connection

    data = await request.read()

    await visualiser_connection.send_str(data.decode("utf-8"))

    return web.Response(text="Ok")


class DebugViewerServer(Thread):
    def __init__(self):
        Thread.__init__(self)
        self.id = 0
        self.sock = socket.socket()
        self.sock.bind(('localhost', 8888))

    @staticmethod
    def send_request(data):
        print("send_request", data)
        requests.post("http://localhost:8080/resend_data", data=data)

    @staticmethod
    def recv_all(sock, n):
        buf = bytearray(n)
        view = memoryview(buf)
        while n:
            nbytes = sock.recv_into(view, n)
            if nbytes == 0:
                return None
            view = view[nbytes:]
            n -= nbytes
        return buf

    @staticmethod
    def recv_msg(sock):
        raw_msglen = DebugViewerServer.recv_all(sock, 4)
        if not raw_msglen:
            return None
        msglen = struct.unpack('>I', raw_msglen)[0]
        t = time.time()
        msg = DebugViewerServer.recv_all(sock, msglen)
        print("reading from socket:", len(msg), time.time() - t)
        return msg

    @staticmethod
    def send_frames_number_to_visualiser():
        global frames
        request_thread = Thread(target=DebugViewerServer.send_request,
                                args=(json.dumps({"type": "frames_number",
                                                  "frames_number": len(frames)}),))
        request_thread.start()

    @staticmethod
    def send_reset_to_visualiser():
        global frames
        request_thread = Thread(target=DebugViewerServer.send_request,
                                args=(json.dumps({"type": "reset"}),))
        request_thread.start()

    def run(self):
        global visualiser_connection
        global frames
        while True:
            print("start listen")
            self.sock.listen(1)
            conn, addr = self.sock.accept()
            self.id = 0

            print('connected to client:', addr)

            frames.clear()

            if visualiser_connection is not None:
                DebugViewerServer.send_reset_to_visualiser()

            while True:
                data = DebugViewerServer.recv_msg(conn)

                if not data:
                    break

                frames.append(data)

                if visualiser_connection is not None:
                    DebugViewerServer.send_frames_number_to_visualiser()

            conn.close()
            print("connection closed")


server = DebugViewerServer()
server.start()

app = web.Application(client_max_size=0)
app.add_routes([web.get('/', visualiser_page)])
app.add_routes([web.post('/resend_data', resend_data)])
app.add_routes([web.get('/send_message_to_visualiser', send_message_to_visualiser_handler)])

web.run_app(app)
