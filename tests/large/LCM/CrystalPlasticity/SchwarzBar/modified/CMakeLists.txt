
# Create a symbolic link for exodiff
execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
                ${SEACAS_EXODIFF} ${CMAKE_CURRENT_BINARY_DIR}/exodiff)
execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
	        ${SEACAS_EPU} ${CMAKE_CURRENT_BINARY_DIR}/epu)

# Copy Input files from source to binary dir

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/coupling.yaml
               ${CMAKE_CURRENT_BINARY_DIR}/coupling.yaml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/coupling.exodiff
               ${CMAKE_CURRENT_BINARY_DIR}/coupling.exodiff COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/coupling.gold.exo
               ${CMAKE_CURRENT_BINARY_DIR}/coupling.gold.exo COPYONLY)

# Add the tests
add_test(ScharzBarCPModified_np1 python ../../run_exodiff_test.py "${SerialAlbanyT.exe}" coupling.yaml)
