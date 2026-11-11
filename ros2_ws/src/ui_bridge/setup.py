from setuptools import setup

setup(name='ui_bridge', version='0.1.0', packages=['ui_bridge'],
      tests_require=['pytest'],
      data_files=[('share/ament_index/resource_index/packages', ['resource/ui_bridge']),
                  ('share/ui_bridge', ['package.xml'])],
      entry_points={'console_scripts': ['ui_bridge = ui_bridge.app:main']})
