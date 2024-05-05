document.addEventListener("DOMContentLoaded", function() {
    var bodyId = document.body.id;
    switch(bodyId) {
        case "index":
            fetchData("ac_settings", "settings");
            break;
        case "page2":
            fetchData("get_settings_page2", "someOtherElement");
            break;
        // Add more cases as needed
    }
});
function fetchData(url, elementId) {
    var targetElement = document.getElementById(elementId);
    targetElement.innerHTML = "Loading..."; // Display a loading message

    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
        if (this.readyState == 4) { // Check if the request is complete
            if (this.status == 200) {
                targetElement.innerHTML = this.responseText;
            } else {
                targetElement.innerHTML = "Error loading data."; // Display error message
            }
        }
    };
    xhttp.open("GET", url, true);
    xhttp.send();
}