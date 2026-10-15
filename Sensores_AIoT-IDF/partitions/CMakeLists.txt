# File: partitions/CMakeLists.txt
# Description: Folder with the partition tables (CSV) of the project.
#              It registers no source code; the table in use is selected in
#              sdkconfig.defaults (CONFIG_PARTITION_TABLE_CUSTOM_FILENAME).
# Standards: ESP-IDF v5.5.1

idf_component_register()
