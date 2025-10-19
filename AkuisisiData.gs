function getAllData() {
  var spreadSheet = SpreadsheetApp.getActiveSpreadsheet();
  var activeSheet = spreadSheet.getActiveSheet();

  // Firebase URL
  var firebaseUrl = "https://hidroponik-f05fd-default-rtdb.firebaseio.com/";

  // Mendapatkan data dari Firebase
  var dataBase = FirebaseApp.getDatabaseByUrl(firebaseUrl);
  var dataSet = dataBase.getData(); // Mengambil data dari Firebase

  if (!dataSet) {
    Logger.log("Data dari Firebase kosong atau tidak ditemukan.");
    return;
  }

  // Cek apakah ada data kualitas air
  if (dataSet.SensorData) {
    var SensorData = dataSet.SensorData;
    
    // Data yang akan ditambahkan
    var newRow = [
      new Date(),                // Timestamp
      SensorData.PPM || "N/A",  
      SensorData.pH || "N/A",   
      SensorData.suhuAir || "N/A",  
      SensorData.statusP1 || "N/A",   // status pompa pH up
      SensorData.statusP2 || "N/A",  
      SensorData.Status || "N/A",   // kualitas air
      SensorData.waktuP1 || "N/A", // Waktu pompa up
      SensorData.waktuP2 || "N/A", 
      SensorData.OutputPompa1Up || "N/A", //  z-waktu pompa 1 pH up
      SensorData.OutputPompa2Down|| "N/A",  
      SensorData.OutputKualitasAir || "N/A"
    ];

    // Menambahkan data ke baris terakhir
    activeSheet.appendRow(newRow);

    Logger.log("Data berhasil ditambahkan ke Google Sheets.");
  } else {
    Logger.log("Tidak ada data SensorData di Firebase.");
  }
}
