from pathlib import Path
import re
import argparse
from datetime import time as dt_time, timedelta, datetime


class Time:
    def __init__(self, ts: str) -> None:
        hour = int(tuple(ts.split(':'))[0])
        minute = int(tuple(ts.split(':'))[1])
        second = int(tuple(ts.split(':'))[2].split(',')[0])
        try:
            microsecond = int(tuple(ts.split(':'))[2].split(',')[1])
        except IndexError:
            microsecond = 0

        self._time = dt_time(hour, minute, second, microsecond)

        return

    def __ge__(self, other: 'Time'):
        return self._time >= other._time

    def __le__(self, other: 'Time'):
        return self._time <= other._time

    def __repr__(self) -> str:
        return f'{self._time.hour:02}:{self._time.minute:02}:{self._time.second:02},{self._time.microsecond:03}'

    @property
    def microseconds(self) -> int:
        return (self._time.hour * 3600000000 + self._time.minute * 60000000 +
                self._time.second * 1000000 + self._time.microsecond)

    def modify_time(self, amount: float) -> None:
        if amount >= 0:
            t = timedelta(seconds=amount)
            dt = datetime.combine(datetime.today(), self._time)
            dt += t
            assert dt.time() > self._time
            self._time = dt.time()
        else:
            t = timedelta(seconds=amount * -1)
            dt = datetime.combine(datetime.today(), self._time)
            dt -= t
            assert dt.time() < self._time
            self._time = dt.time()

        return


def try_encoding(path: Path) -> str:
    encoding = ['utf-8', 'windows-1252']

    for i in encoding:
        try:
            with open(path, 'r', encoding=i) as f:
                f.read()
            return i
        except ValueError:
            pass

    return encoding[0]


def read_file(path: Path) -> str:
    assert path.suffix == '.srt', f"'{str(path)}' is not a subtitle file."

    encoding = try_encoding(path)
    with open(path, 'r', errors='replace', encoding=encoding) as file:
        while True:
            line = file.readline()

            if not line:
                break

            yield line


def within_bounds(begin: str, bounds: tuple) -> bool:
    time = Time(begin.split('-->')[0].strip())

    return all([time >= bounds[0], time <= bounds[1]])


def is_timestamp(line: str) -> bool:
    return bool(re.match(r'^\d{2}:\d{2}:\d{2},\d{3} --> \d{2}:\d{2}:\d{2},\d{3}$', line))


def fix_synchronisation(line: str, amount: float, bounds: tuple) -> str:
    if not is_timestamp(line):
        return line

    if not within_bounds(line, bounds):
        return line

    result = ''
    times = line.split('-->')

    for i in times:
        time = Time(i)

        time.modify_time(amount)

        result += str(time)

        if i == times[-1]:
            result += '\n'
        else:
            result += ' --> '

    return result


def valid_timestamp(arg: str) -> bool:
    return bool(re.match(r'^\d+:[0-5][0-9]:[0-5][0-9]($|.\d+$)', arg))


def resync_subs(args) -> None:
    assert valid_timestamp(args.begin), f'Invalid timestamp: {args.begin}'
    assert valid_timestamp(args.end), f'Invalid timestamp: {args.end}'
    assert args.amount, 'No amount entered'

    read = Path(args.filename)
    if args.output:
        assert Path(args.output).suffix == '.srt', f'Not .srt file: {args.output}'
        write = args.output
    else:
        write = Path(f'{read.stem}_copy{read.suffix}')

    with open(write, 'w') as file:
        for line in read_file(read):
            line = fix_synchronisation(line, -3, (Time(args.begin), Time(args.end)))
            file.write(line)

    return


def find_overlap(args) -> None:
    read = Path(args.filename)

    timestamps: list[tuple[Time, ...]] = []

    for line in read_file(read):
        if not is_timestamp(line):
            continue
        timestamps.append(tuple(map(Time, line.split('-->'))))

    for i in range(len(timestamps[:-1])):
        begin = timestamps[i][0]
        end = timestamps[i][1]

        if begin.microseconds > end.microseconds:
            print(f'{begin} overlaps with {end}')

        end = timestamps[i][1]
        begin = timestamps[i+1][0]

        if end.microseconds > begin.microseconds:
            print(f'{end} overlaps with {begin}')

    return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('-t', '--tool', type=str, default='resync')
    parser.add_argument('-b', '--begin', type=str, default='00:00:00,000000')
    parser.add_argument('-e', '--end', type=str, default='23:59:59,999999')
    parser.add_argument('-f', '--filename', type=str)
    parser.add_argument('-o', '--output', type=str)
    parser.add_argument('-a', '--amount', type=float)
    args = parser.parse_args()

    assert Path(args.filename).is_file(), f'File not found: {args.filename}'
    assert Path(args.filename).suffix == '.srt', f'Not .srt file: {args.filename}'

    if args.tool == 'resync':
        resync_subs(args)
    elif args.tool == 'overlap':
        find_overlap(args)

    return


if __name__ == '__main__':
    main()
