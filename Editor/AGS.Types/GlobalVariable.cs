using System;
using System.Collections.Generic;
using System.Text;
using System.Xml;

namespace AGS.Types
{
    /// <summary>
    /// VariableArrayType provides variable's qualification as an array.
    /// </summary>
    public enum VariableArrayType
    {
        None, // not an array, regular variable
        Array, // static, fixed-sized array
        DynamicArray // dynamic, variable sized array
    }

    public class GlobalVariable
    {
        private string _name = string.Empty;
        private string _type = string.Empty;
        private string _defaultValue = string.Empty;
        private VariableArrayType _arrayType = VariableArrayType.None;
        private int _arraySize = 0;

        public GlobalVariable() { }

        public string Name
        {
            get { return _name; }
            set { _name = value; }
        }

        public string Type
        {
            get { return _type; }
            set { _type = value; }
        }

        public string DefaultValue
        {
            get { return _defaultValue; }
            set { _defaultValue = value; }
        }

        public VariableArrayType ArrayType
        {
            get { return _arrayType; }
            set { _arrayType = value; }
        }

        public int ArraySize
        {
            get { return _arraySize; }
            set { _arraySize = value; }
        }

        public GlobalVariable(XmlNode node)
        {
            SerializeUtils.DeserializeFromXML(this, node);
        }

        public void ToXml(XmlTextWriter writer)
        {
            SerializeUtils.SerializeToXML(this, writer);
        }
    }
}
