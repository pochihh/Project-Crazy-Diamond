import os
from pathlib import Path
import rclpy
from interfaces.srv import LoadCsv, LoadProgram, ListPrograms
from data_tools.csv_program import parse_csv, library_path, MAX_BYTES
from data_tools.ros_client import Operations


class DataServer(Operations):
    def __init__(self):
        super().__init__('data_server')
        self.directory = Path(self.declare_parameter('program_directory', 'data/programs').value)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.busy = False
        self.loader = self.create_client(LoadProgram, '/data/program', callback_group=self.group)
        self.create_service(LoadCsv, '/data/load_csv', self.load, callback_group=self.group)
        self.create_service(ListPrograms, '/data/programs', self.list_files, callback_group=self.group)

    def list_files(self, request, response):
        for path in sorted(self.directory.glob('*.csv')):
            if path.is_file() and not path.is_symlink():
                response.names.append(path.name)
                response.sizes.append(path.stat().st_size)
        return response

    async def load(self, request, response):
        if self.busy:
            response.message = 'Another CSV operation is in progress'
            return response
        self.busy = True
        try:
            path = library_path(self.directory, request.name)
            upload = bool(request.csv)
            if upload:
                content = request.csv
            else:
                if path.stat().st_size > MAX_BYTES:
                    raise ValueError('CSV exceeds 32 MB')
                content = path.read_text()
            channels, values, warnings = parse_csv(content)
            response.warnings = warnings
            response.channels = channels
            if upload:
                if path.exists() and not request.overwrite:
                    raise ValueError('File already exists; choose another name or explicitly replace it')
                temporary = path.with_suffix('.csv.tmp')
                temporary.write_text(content)
                os.replace(temporary, path)
            result = await self.request(self.loader, LoadProgram.Request(name=path.name, channels=channels, values=values))
            response.accepted = True
            response.command_id = result.command_id
            response.rows = len(values)//len(channels)
            response.message = 'Loaded; one row per 0.5 ms tick'
        except Exception as error:
            response.message = str(error)
        finally:
            self.busy = False
        return response


def main():
    rclpy.init()
    node = DataServer()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
