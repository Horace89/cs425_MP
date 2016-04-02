import random
import multiprocessing
import threading
import socket
from message import Message, MulticastMessage
from channel import Channel


class CausalOrderChannel(Channel):
    """
        This is a channel supports delayed function, unicast and causal order multicast.
    """

    def __init__(self, process, pid, socket, process_info, addr_dict):
        super(CausalOrderChannel, self).__init__(process, pid, socket, process_info, addr_dict)

        self.vector_ts = multiprocessing.Array('i', [0] * len(process_info))
        self.hb_queue = [] # hold back queue

    def unicast(self, message, destination):
        if not isinstance(message, MulticastMessage):
            message = Message(self.pid, destination, message)
        print(message.send_str())

        delay_time = random.uniform(self.min_delay, self.max_delay)

        # if the message is sent to itself, there is no delay.
        if destination == self.pid:
            delay_time = 0.0
        print('delay unicast with {0:.2f}s '.format(delay_time))

        # test happen before
        # if self.pid == 2:
        #     if destination == 1:
        #         delay_time = 5.0
        #     else:
        #         delay_time = 0.1
        #     delayed_t = threading.Timer(delay_time, self.__unicast, (message, destination, ))
        #     delayed_t.start()
        # else:
        #     delayed_t = threading.Timer(delay_time, self.__unicast, (message, destination, ))
        #     delayed_t.start()

        delayed_t = threading.Timer(delay_time, self.__unicast, (message, destination,))
        delayed_t.start()

    def __unicast(self, message, destination):
        dest_addr = self.process_info[destination]
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        try:
            sock.sendto(str(message), dest_addr)
            # data, server = sock.recvfrom(4096)
        finally:
            sock.close()

    def multicast(self, message):
        with self.vector_ts.get_lock():
            self.vector_ts[self.pid - 1] += 1

        for to_pid in self.process_info.keys():
            m = MulticastMessage(self.pid, to_pid, [x for x in self.vector_ts], message)
            self.unicast(m, to_pid)

    def recv(self, data, from_addr):
        if data:
            data_args = data.split()
            # Unicast Receive
            if len(data_args) < 4:
                m = Message(data_args[0], data_args[1], data_args[2])
                self.process.unicast_receive(m.from_id, m)

            # Multicast Receive
            else:
                from_id, to_id, receive_vector_ts, message = \
                    int(data_args[0]), int(data_args[1]), ([int(x) for x in data_args[3].split(',')]), data_args[2]
                m = MulticastMessage(from_id, to_id, receive_vector_ts, message)

                # if the message is from itself, we can deliver it immediately
                if from_id == self.pid:
                    self.process.unicast_receive(m.from_id, m)
                else:
                    # Check the vector clock to determine if we need to put it into the hold_back queue
                    if self.check_order(receive_vector_ts, from_id):

                        # Update the local vector_timestamp
                        with self.vector_ts.get_lock():
                            self.vector_ts[from_id - 1] += 1

                        # Deliver the message to process
                        self.process.unicast_receive(m.from_id, m)

                        # Check the hold_back queue
                        queued_message = self.check_queue()
                        while queued_message:

                            # Deliver the message to process
                            self.process.unicast_receive(queued_message.from_id, queued_message)
                            queued_message = self.check_queue()
                    else:
                        # The message received should be pushed into the hold_back queue
                        self.hb_queue.append(m)

    """
     Compare with a given vector timestamp with local vector timestamp based on casual order algorithm
     Return true if we can accept the vector timestamp
     Otherwise, return false.
    """
    def check_order(self, vector_ts, pid):
        if vector_ts[pid - 1] != (self.vector_ts[pid - 1] + 1):
            return False

        for i, v in enumerate(vector_ts):
            if (pid - 1) != i:
                if not vector_ts[i] <= self.vector_ts[i]:
                    return False
        return True

    # Check if the process received a message with appropriate vector timestamp.
    def check_queue(self):
        if self.hb_queue:
            for queued_message in self.hb_queue:
                if self.check_order(queued_message.vector_ts, queued_message.from_id):
                    with self.vector_ts.get_lock():
                        self.vector_ts[queued_message.from_id - 1] += 1
                    self.hb_queue.remove(queued_message)
                    return queued_message
            return None
        else:
            return None