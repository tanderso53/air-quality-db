cmake_minimum_required(VERSION 3.17)

project(air-quality-db VERSION 1.0.0
	LANGUAGES C)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
	src/air-quality-db.c)

add_executable(aqdb-add-device
	src/aqdb-add-device.c)

target_link_libraries(aqdb-add-device PRIVATE
	pq)

target_link_directories(aqdb-add-device PRIVATE
	/usr/local/lib)
