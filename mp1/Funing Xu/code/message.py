from datetime import datetime


class Message(object):
    """
        Helper classes to encode/decode strings
    """

    def __init__(self, from_id, to_id, content=''):
        self.from_id = from_id
        self.to_id = to_id
        self.content = content

    def send_str(self):
        return 'Sent "{}" to process {}, system time is {}'.format(
            self.content, self.to_id, str(datetime.now()))

    def receive_str(self):
        return 'Received "{}" from process {}, system time is {}'.format(
            self.content, self.from_id, str(datetime.now()))

    def __str__(self):
        return '{} {} {}'.format(self.from_id, self.to_id, self.content)


class MulticastMessage(Message):

    def __init__(self, from_id, to_id, vector_ts, content=''):
        super(MulticastMessage, self).__init__(from_id, to_id, content)
        self.vector_ts = vector_ts

    def __str__(self):
        return '{} {} {} {}'.format(self.from_id, self.to_id, self.content, ','.join([str(x) for x in self.vector_ts]))


class TotalOrderMessage(Message):

    def __init__(self, from_id, to_id, id, content=''):
        super(TotalOrderMessage, self).__init__(from_id, to_id, content)
        self.id = id

    def __str__(self):
        return '{} {} {} {}'.format(self.from_id, self.to_id, self.content, self.id)


class SqeuncerMessage:

    def __init__(self, id, sequence):
        self.id = id
        self.sequence = sequence

    def send_str(self):
        return 'Sent sequencer message'

    def __str__(self):
        return '{} {}'.format(self.id, self.sequence)