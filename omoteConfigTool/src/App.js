import Tab from 'react-bootstrap/Tab';
import Tabs from 'react-bootstrap/Tabs';

function FillExample() {
  return (
    <Tabs
      defaultActiveKey="profile"
      id="fill-tab-example"
      className="mb-3"
      fill
    >
      <Tab eventKey="Global" title="Global">
        Tab content for Home
      </Tab>
      <Tab eventKey="Scenes" title="Scenes">
        Tab content for Profile
      </Tab>
      <Tab eventKey="Devices" title="Devices">
        Tab content for Loooonger Tab
      </Tab>
      <Tab eventKey="GUIs" title="guis">
        Tab content for Contact
      </Tab>
    </Tabs>
  );
}

export default FillExample;