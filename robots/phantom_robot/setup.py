from setuptools import setup
setup(name='phantom_robot',version='0.1.0',packages=['phantom_robot'],package_data={'phantom_robot':['static/*']},tests_require=['pytest'],data_files=[('share/ament_index/resource_index/packages',['resource/phantom_robot']),('share/phantom_robot',['package.xml'])],entry_points={'console_scripts':['phantom_robot = phantom_robot.node:main']})
