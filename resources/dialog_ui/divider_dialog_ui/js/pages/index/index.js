/*
    Copyright (c) 2022 Huawei Device Co., Ltd.
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

export default {
    data: {
        tag : "system.window.divider:",
        backgroundColor:"black",
        buttonWidth:"",
        buttonHeight:"",
        flexDirection:"",
    },
    onInit() {
        console.info(this.tag + "on init")
    },
    onDoubleClick() {
        console.info(this.tag + 'on double click');
    },
    onDialogUpdated(param) {
        /* update view style */
        console.info(this.tag + 'on dialog update param width: ' + param.width);
        console.info(this.tag + 'on dialog update param height: ' + param.height);
        if (param.width < param.height) {
            this.buttonWidth = "90%"
            this.buttonHeight = "10%"
            this.flexDirection = "row"
        } else {
            this.buttonWidth = "10%"
            this.buttonHeight = "90%"
            this.flexDirection = "column"
        }
    }
}