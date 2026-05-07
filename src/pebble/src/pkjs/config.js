module.exports = [
  {
    "type": "heading",
    "defaultValue": "NirvanaHQ Configuration"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Authentication"
      },
      {
        "type": "input",
        "messageKey": "username",
        "defaultValue": "",
        "label": "Email Address"
      },
      {
        "type": "input",
        "messageKey": "password",
        "defaultValue": "",
        "label": "Password",
        "attributes": {
          "type": "password"
        }
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
