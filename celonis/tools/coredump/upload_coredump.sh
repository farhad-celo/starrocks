#!/usr/bin/bash

log_stdin()
{
    echo "[coredump] $@" >&1
}

COREDUMP_PATH=/opt/starrocks/be/storage/coredumps

if [ ! -d ${COREDUMP_PATH} ]; then
    mkdir -p ${COREDUMP_PATH}
fi

cd $COREDUMP_PATH

while true; do

  latestCoreFile=""

  # Setup inotifywait loop to wait until core file has been completely written
  while read -r path action file; do
    log_stdin "${action} ${path}${file}"

    if [[ "$file" = core.*  ]]; then
      latestCoreFile=$file
    fi
  done < <(inotifywait -e close_write $COREDUMP_PATH)

  if [[ $latestCoreFile == "" ]]; then
    log_stdin "missiong core file name"

    continue
  fi

  log_stdin "Core dump generated $latestCoreFile"
  log_stdin "$(ls -lh ${latestCoreFile})"

  fileSize=$(ls -lh "${latestCoreFile}" | awk '{print $5}')
  fileSizeBytes=$(stat -c %s "${latestCoreFile}")


  # Check if file size is greater than 1TB (1000 GB)
  if [[ "$fileSize" =~ T$ ]]; then
      log_stdin "File size (${fileSizeBytes} B) exceeds 1TB: ${fileSizeBytes} bytes, skip"
      rm ${latestCoreFile}

      continue
  fi


  DATESTR=`TZ=${TZ} date +"%m-%d-%y.%H-%M-%S.%Z"`
  COREDUMP_FILE_ZIP=${KUBE_CLUSTER_NAME}.${POD_NAMESPACE}.${POD_NAME}.${SR_IMAGE_TAG}.${DATESTR}.gz

  log_stdin "Compressing core dump files to ${COREDUMP_FILE_ZIP} ..."
  pigz -c $latestCoreFile > $COREDUMP_FILE_ZIP


  log_stdin "Zip complete"
  log_stdin "$(ls -lh ${COREDUMP_FILE_ZIP})"


  rclone --config=/opt/starrocks/rclone.conf --bwlimit 1000M --multi-thread-streams 100 --multi-thread-cutoff 8M --progress sync $COREDUMP_FILE_ZIP coredump:${COREDUMP_BLOBSTORE_PREFIX}/${KUBE_CLUSTER_NAME}/${POD_NAMESPACE}


  log_stdin "Upload complete: ${latestCoreFile}"

  # Only clean the uploaded core dump file
  rm $latestCoreFile $COREDUMP_FILE_ZIP

done
