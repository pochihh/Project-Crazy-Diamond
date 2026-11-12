from setuptools import setup
setup(name='data_tools',version='0.1.0',packages=['data_tools'],tests_require=['pytest'],data_files=[('share/ament_index/resource_index/packages',['resource/data_tools']),('share/data_tools',['package.xml'])],entry_points={'console_scripts':['data_server = data_tools.server:main']})
